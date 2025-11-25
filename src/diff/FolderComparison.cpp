#include "opacity/diff/FolderComparison.h"
#include "opacity/core/Logger.h"
#include "opacity/filesystem/FileSystemManager.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>

// For hashing
#include <array>

namespace opacity::diff
{
    // ComparisonItem implementation
    bool ComparisonItem::IsLeftNewer() const
    {
        return left_modified > right_modified;
    }

    // FolderComparisonResult implementation
    std::vector<const ComparisonItem*> FolderComparisonResult::GetByStatus(ComparisonStatus status) const
    {
        std::vector<const ComparisonItem*> result;
        for (const auto& item : items)
        {
            if (item.status == status)
            {
                result.push_back(&item);
            }
        }
        return result;
    }

    std::vector<const ComparisonItem*> FolderComparisonResult::GetDifferences() const
    {
        std::vector<const ComparisonItem*> result;
        for (const auto& item : items)
        {
            if (item.status != ComparisonStatus::Identical)
            {
                result.push_back(&item);
            }
        }
        return result;
    }

    bool FolderComparisonResult::AreIdentical() const
    {
        return success && stats.different_files == 0 && 
               stats.left_only_files == 0 && stats.right_only_files == 0 &&
               stats.left_only_dirs == 0 && stats.right_only_dirs == 0;
    }

    // FolderComparison implementation
    FolderComparison::FolderComparison() = default;

    FolderComparison::~FolderComparison()
    {
        Cancel();
        if (worker_thread_ && worker_thread_->joinable())
        {
            worker_thread_->join();
        }
    }

    FolderComparisonResult FolderComparison::Compare(
        const core::Path& left_path,
        const core::Path& right_path,
        const FolderComparisonOptions& options,
        ComparisonProgressCallback progress_callback)
    {
        auto start_time = std::chrono::steady_clock::now();
        
        FolderComparisonResult result;
        result.left_root = left_path;
        result.right_root = right_path;
        result.options = options;

        running_.store(true);
        cancel_requested_.store(false);

        SPDLOG_INFO("Starting folder comparison: {} vs {}", left_path.String(), right_path.String());

        // Validate paths
        if (!std::filesystem::exists(left_path.Get()))
        {
            result.error_message = "Left folder does not exist: " + left_path.String();
            SPDLOG_ERROR(result.error_message);
            running_.store(false);
            return result;
        }

        if (!std::filesystem::exists(right_path.Get()))
        {
            result.error_message = "Right folder does not exist: " + right_path.String();
            SPDLOG_ERROR(result.error_message);
            running_.store(false);
            return result;
        }

        // Enumerate both folders
        auto left_items = EnumerateFolder(left_path, options);
        auto right_items = EnumerateFolder(right_path, options);

        if (cancel_requested_.load())
        {
            result.error_message = "Comparison cancelled";
            running_.store(false);
            return result;
        }

        // Build lookup maps
        std::unordered_map<std::string, filesystem::FsItem> left_map;
        std::unordered_map<std::string, filesystem::FsItem> right_map;

        for (const auto& [rel_path, item] : left_items)
        {
            std::string key = options.ignore_case ? 
                [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }(rel_path) : rel_path;
            left_map[key] = item;
        }

        for (const auto& [rel_path, item] : right_items)
        {
            std::string key = options.ignore_case ? 
                [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }(rel_path) : rel_path;
            right_map[key] = item;
        }

        // Collect all unique paths
        std::set<std::string> all_paths;
        for (const auto& [path, _] : left_map) all_paths.insert(path);
        for (const auto& [path, _] : right_map) all_paths.insert(path);

        size_t total = all_paths.size();
        size_t processed = 0;

        // Compare items
        for (const auto& rel_path : all_paths)
        {
            if (cancel_requested_.load())
            {
                result.error_message = "Comparison cancelled";
                break;
            }

            ComparisonItem item;
            item.relative_path = rel_path;

            auto left_it = left_map.find(rel_path);
            auto right_it = right_map.find(rel_path);

            if (left_it != left_map.end())
            {
                item.left_exists = true;
                item.left_size = left_it->second.size;
                item.left_modified = left_it->second.modified_time;
                item.left_is_directory = left_it->second.is_directory;
            }

            if (right_it != right_map.end())
            {
                item.right_exists = true;
                item.right_size = right_it->second.size;
                item.right_modified = right_it->second.modified_time;
                item.right_is_directory = right_it->second.is_directory;
            }

            // Determine status
            if (item.left_exists && item.right_exists)
            {
                if (item.left_is_directory && item.right_is_directory)
                {
                    item.status = ComparisonStatus::Identical;
                    ++result.stats.identical_dirs;
                }
                else if (item.left_is_directory != item.right_is_directory)
                {
                    item.status = ComparisonStatus::Different;
                    ++result.stats.different_files;
                }
                else
                {
                    // Both are files - compare based on mode
                    core::Path left_full(left_path.String() + "/" + rel_path);
                    core::Path right_full(right_path.String() + "/" + rel_path);
                    item.status = CompareFiles(left_full, right_full, options.mode, item);
                    
                    if (item.status == ComparisonStatus::Identical)
                    {
                        ++result.stats.identical_files;
                    }
                    else if (item.status == ComparisonStatus::Different)
                    {
                        ++result.stats.different_files;
                        result.stats.different_size += std::max(item.left_size, item.right_size);
                    }
                    else if (item.status == ComparisonStatus::Error)
                    {
                        ++result.stats.errors;
                    }
                }
            }
            else if (item.left_exists)
            {
                item.status = ComparisonStatus::LeftOnly;
                if (item.left_is_directory)
                    ++result.stats.left_only_dirs;
                else
                    ++result.stats.left_only_files;
            }
            else
            {
                item.status = ComparisonStatus::RightOnly;
                if (item.right_is_directory)
                    ++result.stats.right_only_dirs;
                else
                    ++result.stats.right_only_files;
            }

            result.stats.left_total_size += item.left_size;
            result.stats.right_total_size += item.right_size;
            result.items.push_back(item);
            ++processed;

            // Report progress
            if (progress_callback)
            {
                ComparisonProgress progress;
                progress.files_processed = processed;
                progress.total_files = total;
                progress.current_file = rel_path;
                progress.percentage = total > 0 ? (static_cast<double>(processed) / total) * 100.0 : 0.0;
                progress_callback(progress);
            }
        }

        result.stats.total_items = result.items.size();
        
        auto end_time = std::chrono::steady_clock::now();
        result.stats.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        result.success = !cancel_requested_.load();
        
        SPDLOG_INFO("Folder comparison complete: {} items, {} identical, {} different, {} left-only, {} right-only",
            result.stats.total_items, result.stats.identical_files, result.stats.different_files,
            result.stats.left_only_files, result.stats.right_only_files);

        running_.store(false);

        std::lock_guard<std::mutex> lock(result_mutex_);
        current_result_ = result;

        return result;
    }

    void FolderComparison::CompareAsync(
        const core::Path& left_path,
        const core::Path& right_path,
        const FolderComparisonOptions& options,
        ComparisonProgressCallback progress_callback,
        ComparisonCompleteCallback complete_callback)
    {
        if (running_.load())
        {
            if (complete_callback)
            {
                complete_callback(false, "Comparison already in progress");
            }
            return;
        }

        // Join any previous thread
        if (worker_thread_ && worker_thread_->joinable())
        {
            worker_thread_->join();
        }

        worker_thread_ = std::make_unique<std::thread>([this, left_path, right_path, options, 
                                                         progress_callback, complete_callback]()
        {
            auto result = Compare(left_path, right_path, options, progress_callback);
            
            if (complete_callback)
            {
                complete_callback(result.success, result.error_message);
            }
        });
    }

    void FolderComparison::Cancel()
    {
        cancel_requested_.store(true);
    }

    FolderComparisonResult FolderComparison::GetCurrentResult() const
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        return current_result_;
    }

    SyncResult FolderComparison::SyncFolders(
        const FolderComparisonResult& result,
        SyncDirection direction,
        const std::vector<size_t>* selected_items)
    {
        SyncResult sync_result;
        filesystem::FileSystemManager fs_manager;

        std::vector<size_t> items_to_sync;
        if (selected_items)
        {
            items_to_sync = *selected_items;
        }
        else
        {
            for (size_t i = 0; i < result.items.size(); ++i)
            {
                if (result.items[i].status != ComparisonStatus::Identical)
                {
                    items_to_sync.push_back(i);
                }
            }
        }

        for (size_t idx : items_to_sync)
        {
            if (idx >= result.items.size()) continue;
            
            const auto& item = result.items[idx];
            core::Path left_path(result.left_root.String() + "/" + item.relative_path);
            core::Path right_path(result.right_root.String() + "/" + item.relative_path);

            try
            {
                switch (direction)
                {
                case SyncDirection::LeftToRight:
                    if (item.left_exists && !item.left_is_directory)
                    {
                        if (item.right_exists)
                        {
                            if (fs_manager.Copy(left_path, right_path, true))
                                ++sync_result.files_updated;
                        }
                        else
                        {
                            // Create parent directories if needed
                            auto parent = right_path.Parent();
                            if (!std::filesystem::exists(parent.Get()))
                            {
                                std::filesystem::create_directories(parent.Get());
                            }
                            if (fs_manager.Copy(left_path, right_path, false))
                                ++sync_result.files_copied;
                        }
                    }
                    break;

                case SyncDirection::RightToLeft:
                    if (item.right_exists && !item.right_is_directory)
                    {
                        if (item.left_exists)
                        {
                            if (fs_manager.Copy(right_path, left_path, true))
                                ++sync_result.files_updated;
                        }
                        else
                        {
                            auto parent = left_path.Parent();
                            if (!std::filesystem::exists(parent.Get()))
                            {
                                std::filesystem::create_directories(parent.Get());
                            }
                            if (fs_manager.Copy(right_path, left_path, false))
                                ++sync_result.files_copied;
                        }
                    }
                    break;

                case SyncDirection::Bidirectional:
                    if (item.status == ComparisonStatus::LeftOnly)
                    {
                        auto parent = right_path.Parent();
                        if (!std::filesystem::exists(parent.Get()))
                        {
                            std::filesystem::create_directories(parent.Get());
                        }
                        if (fs_manager.Copy(left_path, right_path, false))
                            ++sync_result.files_copied;
                    }
                    else if (item.status == ComparisonStatus::RightOnly)
                    {
                        auto parent = left_path.Parent();
                        if (!std::filesystem::exists(parent.Get()))
                        {
                            std::filesystem::create_directories(parent.Get());
                        }
                        if (fs_manager.Copy(right_path, left_path, false))
                            ++sync_result.files_copied;
                    }
                    else if (item.status == ComparisonStatus::Different)
                    {
                        // Copy newer to older
                        if (item.IsLeftNewer())
                        {
                            if (fs_manager.Copy(left_path, right_path, true))
                                ++sync_result.files_updated;
                        }
                        else
                        {
                            if (fs_manager.Copy(right_path, left_path, true))
                                ++sync_result.files_updated;
                        }
                    }
                    break;

                case SyncDirection::Mirror:
                    if (item.left_exists && !item.left_is_directory)
                    {
                        auto parent = right_path.Parent();
                        if (!std::filesystem::exists(parent.Get()))
                        {
                            std::filesystem::create_directories(parent.Get());
                        }
                        if (fs_manager.Copy(left_path, right_path, true))
                        {
                            if (item.right_exists)
                                ++sync_result.files_updated;
                            else
                                ++sync_result.files_copied;
                        }
                    }
                    else if (!item.left_exists && item.right_exists)
                    {
                        if (fs_manager.Delete(right_path, item.right_is_directory))
                            ++sync_result.files_deleted;
                    }
                    break;
                }
            }
            catch (const std::exception& e)
            {
                ++sync_result.errors;
                sync_result.error_messages.push_back(item.relative_path + ": " + e.what());
            }
        }

        sync_result.success = sync_result.errors == 0;
        return sync_result;
    }

    std::string FolderComparison::ExportToCsv(const FolderComparisonResult& result) const
    {
        std::ostringstream ss;
        ss << "Path,Status,Left Size,Right Size,Left Modified,Right Modified\n";

        for (const auto& item : result.items)
        {
            ss << "\"" << item.relative_path << "\",";
            
            switch (item.status)
            {
            case ComparisonStatus::Identical: ss << "Identical"; break;
            case ComparisonStatus::Different: ss << "Different"; break;
            case ComparisonStatus::LeftOnly: ss << "Left Only"; break;
            case ComparisonStatus::RightOnly: ss << "Right Only"; break;
            case ComparisonStatus::Error: ss << "Error"; break;
            }
            
            ss << "," << item.left_size << "," << item.right_size << ",";
            
            // Format timestamps
            if (item.left_exists)
            {
                auto time = std::chrono::system_clock::to_time_t(item.left_modified);
                ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
            }
            ss << ",";
            
            if (item.right_exists)
            {
                auto time = std::chrono::system_clock::to_time_t(item.right_modified);
                ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
            }
            ss << "\n";
        }

        return ss.str();
    }

    std::string FolderComparison::ExportToHtml(const FolderComparisonResult& result) const
    {
        std::ostringstream ss;
        ss << R"(<!DOCTYPE html>
<html>
<head>
<title>Folder Comparison Results</title>
<style>
body { font-family: Arial, sans-serif; margin: 20px; }
table { border-collapse: collapse; width: 100%; }
th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
th { background-color: #4CAF50; color: white; }
tr:nth-child(even) { background-color: #f2f2f2; }
.identical { color: green; }
.different { color: orange; }
.left-only { color: blue; }
.right-only { color: purple; }
.error { color: red; }
.summary { margin-bottom: 20px; padding: 10px; background-color: #f5f5f5; }
</style>
</head>
<body>
<h1>Folder Comparison Results</h1>
<div class="summary">
<p><strong>Left:</strong> )" << result.left_root.String() << R"(</p>
<p><strong>Right:</strong> )" << result.right_root.String() << R"(</p>
<p><strong>Total Items:</strong> )" << result.stats.total_items << R"(</p>
<p><strong>Identical:</strong> )" << result.stats.identical_files << R"( files, )" << result.stats.identical_dirs << R"( folders</p>
<p><strong>Different:</strong> )" << result.stats.different_files << R"( files</p>
<p><strong>Left Only:</strong> )" << result.stats.left_only_files << R"( files, )" << result.stats.left_only_dirs << R"( folders</p>
<p><strong>Right Only:</strong> )" << result.stats.right_only_files << R"( files, )" << result.stats.right_only_dirs << R"( folders</p>
</div>
<table>
<tr><th>Path</th><th>Status</th><th>Left Size</th><th>Right Size</th></tr>
)";

        for (const auto& item : result.items)
        {
            std::string css_class;
            std::string status_text;
            
            switch (item.status)
            {
            case ComparisonStatus::Identical: css_class = "identical"; status_text = "Identical"; break;
            case ComparisonStatus::Different: css_class = "different"; status_text = "Different"; break;
            case ComparisonStatus::LeftOnly: css_class = "left-only"; status_text = "Left Only"; break;
            case ComparisonStatus::RightOnly: css_class = "right-only"; status_text = "Right Only"; break;
            case ComparisonStatus::Error: css_class = "error"; status_text = "Error"; break;
            }

            ss << "<tr class=\"" << css_class << "\">";
            ss << "<td>" << item.relative_path << "</td>";
            ss << "<td>" << status_text << "</td>";
            ss << "<td>" << (item.left_exists ? std::to_string(item.left_size) : "-") << "</td>";
            ss << "<td>" << (item.right_exists ? std::to_string(item.right_size) : "-") << "</td>";
            ss << "</tr>\n";
        }

        ss << "</table>\n</body>\n</html>";
        return ss.str();
    }

    std::string FolderComparison::ExportToText(const FolderComparisonResult& result) const
    {
        std::ostringstream ss;
        ss << "Folder Comparison Results\n";
        ss << "========================\n\n";
        ss << "Left:  " << result.left_root.String() << "\n";
        ss << "Right: " << result.right_root.String() << "\n\n";
        ss << "Summary:\n";
        ss << "  Total Items: " << result.stats.total_items << "\n";
        ss << "  Identical: " << result.stats.identical_files << " files, " << result.stats.identical_dirs << " folders\n";
        ss << "  Different: " << result.stats.different_files << " files\n";
        ss << "  Left Only: " << result.stats.left_only_files << " files, " << result.stats.left_only_dirs << " folders\n";
        ss << "  Right Only: " << result.stats.right_only_files << " files, " << result.stats.right_only_dirs << " folders\n";
        ss << "  Duration: " << result.stats.duration.count() << " ms\n\n";
        ss << "Details:\n";
        ss << "--------\n";

        for (const auto& item : result.items)
        {
            if (item.status == ComparisonStatus::Identical) continue;
            
            std::string status;
            switch (item.status)
            {
            case ComparisonStatus::Different: status = "[DIFF]"; break;
            case ComparisonStatus::LeftOnly: status = "[LEFT]"; break;
            case ComparisonStatus::RightOnly: status = "[RIGHT]"; break;
            case ComparisonStatus::Error: status = "[ERROR]"; break;
            default: status = "[?]"; break;
            }

            ss << status << " " << item.relative_path << "\n";
        }

        return ss.str();
    }

    std::vector<std::pair<std::string, filesystem::FsItem>> FolderComparison::EnumerateFolder(
        const core::Path& root,
        const FolderComparisonOptions& options,
        size_t current_depth)
    {
        std::vector<std::pair<std::string, filesystem::FsItem>> results;

        if (options.max_depth > 0 && current_depth >= options.max_depth)
        {
            return results;
        }

        try
        {
            for (const auto& entry : std::filesystem::directory_iterator(root.Get()))
            {
                if (cancel_requested_.load()) break;

                std::string name = entry.path().filename().string();
                std::string relative = entry.path().lexically_relative(root.Get()).string();

                // Skip hidden files if not included
                if (!options.include_hidden && name.front() == '.')
                {
                    continue;
                }

                // Check include/exclude patterns
                if (!MatchesPatterns(relative, options.include_patterns, options.exclude_patterns))
                {
                    continue;
                }

                filesystem::FsItem item;
                item.name = name;
                item.path = entry.path().string();
                item.is_directory = entry.is_directory();

                if (!item.is_directory)
                {
                    try
                    {
                        item.size = entry.file_size();
                    }
                    catch (...) { item.size = 0; }
                }

                try
                {
                    auto ftime = std::filesystem::last_write_time(entry.path());
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                    item.modified_time = sctp;
                }
                catch (...) {}

                results.push_back({relative, item});

                // Recurse into directories
                if (item.is_directory && options.recursive)
                {
                    auto sub_items = EnumerateFolder(core::Path(entry.path().string()), options, current_depth + 1);
                    for (auto& [sub_rel, sub_item] : sub_items)
                    {
                        results.push_back({relative + "/" + sub_rel, sub_item});
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            SPDLOG_WARN("Error enumerating folder {}: {}", root.String(), e.what());
        }

        return results;
    }

    ComparisonStatus FolderComparison::CompareFiles(
        const core::Path& left_path,
        const core::Path& right_path,
        ComparisonMode mode,
        ComparisonItem& item)
    {
        try
        {
            switch (mode)
            {
            case ComparisonMode::Name:
                // If we got here, names match
                return ComparisonStatus::Identical;

            case ComparisonMode::Size:
                return (item.left_size == item.right_size) ? 
                    ComparisonStatus::Identical : ComparisonStatus::Different;

            case ComparisonMode::Date:
                // Allow 2 second tolerance for filesystem differences
                {
                    auto diff = std::chrono::abs(item.left_modified - item.right_modified);
                    if (diff <= std::chrono::seconds(2) && item.left_size == item.right_size)
                        return ComparisonStatus::Identical;
                    return ComparisonStatus::Different;
                }

            case ComparisonMode::Hash:
                {
                    item.left_hash = CalculateHash(left_path);
                    item.right_hash = CalculateHash(right_path);
                    return (item.left_hash == item.right_hash) ? 
                        ComparisonStatus::Identical : ComparisonStatus::Different;
                }

            case ComparisonMode::Content:
                {
                    // Quick size check first
                    if (item.left_size != item.right_size)
                        return ComparisonStatus::Different;

                    // Byte-by-byte comparison
                    std::ifstream left_stream(left_path.String(), std::ios::binary);
                    std::ifstream right_stream(right_path.String(), std::ios::binary);

                    if (!left_stream || !right_stream)
                    {
                        item.error_message = "Failed to open files for comparison";
                        return ComparisonStatus::Error;
                    }

                    constexpr size_t BUFFER_SIZE = 64 * 1024;
                    std::vector<char> left_buffer(BUFFER_SIZE);
                    std::vector<char> right_buffer(BUFFER_SIZE);

                    while (left_stream && right_stream)
                    {
                        left_stream.read(left_buffer.data(), BUFFER_SIZE);
                        right_stream.read(right_buffer.data(), BUFFER_SIZE);

                        auto left_read = left_stream.gcount();
                        auto right_read = right_stream.gcount();

                        if (left_read != right_read)
                            return ComparisonStatus::Different;

                        if (left_read == 0) break;

                        if (std::memcmp(left_buffer.data(), right_buffer.data(), left_read) != 0)
                            return ComparisonStatus::Different;
                    }

                    return ComparisonStatus::Identical;
                }
            }
        }
        catch (const std::exception& e)
        {
            item.error_message = e.what();
            return ComparisonStatus::Error;
        }

        return ComparisonStatus::Error;
    }

    std::string FolderComparison::CalculateHash(const core::Path& path) const
    {
        // Simple hash implementation - in production, use a proper crypto library
        std::ifstream file(path.String(), std::ios::binary);
        if (!file)
            return "";

        // Simple FNV-1a hash for demonstration
        uint64_t hash = 14695981039346656037ULL;
        constexpr uint64_t prime = 1099511628211ULL;

        char buffer[8192];
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
        {
            for (std::streamsize i = 0; i < file.gcount(); ++i)
            {
                hash ^= static_cast<uint8_t>(buffer[i]);
                hash *= prime;
            }
        }

        std::ostringstream ss;
        ss << std::hex << std::setfill('0') << std::setw(16) << hash;
        return ss.str();
    }

    bool FolderComparison::MatchesPatterns(
        const std::string& path,
        const std::vector<std::string>& include,
        const std::vector<std::string>& exclude) const
    {
        // If no include patterns, include everything
        bool included = include.empty();

        // Check include patterns
        for (const auto& pattern : include)
        {
            try
            {
                std::regex rx(pattern, std::regex::icase);
                if (std::regex_search(path, rx))
                {
                    included = true;
                    break;
                }
            }
            catch (...) {}
        }

        if (!included) return false;

        // Check exclude patterns
        for (const auto& pattern : exclude)
        {
            try
            {
                std::regex rx(pattern, std::regex::icase);
                if (std::regex_search(path, rx))
                {
                    return false;
                }
            }
            catch (...) {}
        }

        return true;
    }

} // namespace opacity::diff
