#include "opacity/batch/DuplicateFinder.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

// Windows headers for recycle bin
#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

namespace opacity::batch
{
    // DuplicateGroup implementation
    core::Path DuplicateGroup::GetOldestFile() const
    {
        if (files.empty()) return core::Path{};
        
        core::Path oldest = files[0];
        auto oldest_time = std::filesystem::last_write_time(oldest.Get());
        
        for (size_t i = 1; i < files.size(); ++i)
        {
            auto time = std::filesystem::last_write_time(files[i].Get());
            if (time < oldest_time)
            {
                oldest = files[i];
                oldest_time = time;
            }
        }
        
        return oldest;
    }

    core::Path DuplicateGroup::GetNewestFile() const
    {
        if (files.empty()) return core::Path{};
        
        core::Path newest = files[0];
        auto newest_time = std::filesystem::last_write_time(newest.Get());
        
        for (size_t i = 1; i < files.size(); ++i)
        {
            auto time = std::filesystem::last_write_time(files[i].Get());
            if (time > newest_time)
            {
                newest = files[i];
                newest_time = time;
            }
        }
        
        return newest;
    }

    core::Path DuplicateGroup::GetShortestPath() const
    {
        if (files.empty()) return core::Path{};
        
        core::Path shortest = files[0];
        for (size_t i = 1; i < files.size(); ++i)
        {
            if (files[i].String().length() < shortest.String().length())
            {
                shortest = files[i];
            }
        }
        
        return shortest;
    }

    core::Path DuplicateGroup::GetLongestPath() const
    {
        if (files.empty()) return core::Path{};
        
        core::Path longest = files[0];
        for (size_t i = 1; i < files.size(); ++i)
        {
            if (files[i].String().length() > longest.String().length())
            {
                longest = files[i];
            }
        }
        
        return longest;
    }

    // DuplicateResult implementation
    std::vector<const DuplicateGroup*> DuplicateResult::GetByWastedSpace() const
    {
        std::vector<const DuplicateGroup*> result;
        for (const auto& group : groups)
        {
            result.push_back(&group);
        }
        
        std::sort(result.begin(), result.end(),
            [](const DuplicateGroup* a, const DuplicateGroup* b)
            {
                return a->GetWastedSpace() > b->GetWastedSpace();
            });
        
        return result;
    }

    std::vector<const DuplicateGroup*> DuplicateResult::GetByFileCount() const
    {
        std::vector<const DuplicateGroup*> result;
        for (const auto& group : groups)
        {
            result.push_back(&group);
        }
        
        std::sort(result.begin(), result.end(),
            [](const DuplicateGroup* a, const DuplicateGroup* b)
            {
                return a->files.size() > b->files.size();
            });
        
        return result;
    }

    // DuplicateFinder implementation
    DuplicateFinder::DuplicateFinder() = default;

    DuplicateFinder::~DuplicateFinder()
    {
        Cancel();
        if (worker_thread_ && worker_thread_->joinable())
        {
            worker_thread_->join();
        }
    }

    DuplicateResult DuplicateFinder::FindDuplicates(
        const std::vector<core::Path>& paths,
        const DuplicateSearchOptions& options,
        DuplicateProgressCallback progress_callback)
    {
        auto start_time = std::chrono::steady_clock::now();
        
        DuplicateResult result;
        running_.store(true);
        cancel_requested_.store(false);

        SPDLOG_INFO("Starting duplicate search in {} paths", paths.size());

        // Phase 1: Collect all files
        if (progress_callback)
        {
            DuplicateProgress progress;
            progress.current_phase = "Scanning files";
            progress_callback(progress);
        }

        auto files = CollectFiles(paths, options, progress_callback);
        result.total_files_scanned = files.size();

        if (cancel_requested_.load())
        {
            result.error_message = "Search cancelled";
            running_.store(false);
            return result;
        }

        SPDLOG_INFO("Found {} files to check", files.size());

        // Phase 2: Group by size
        if (progress_callback)
        {
            DuplicateProgress progress;
            progress.current_phase = "Grouping by size";
            progress.total_files = files.size();
            progress_callback(progress);
        }

        auto size_groups = GroupBySize(files);

        // Remove groups with only one file (no duplicates possible)
        for (auto it = size_groups.begin(); it != size_groups.end();)
        {
            if (it->second.size() <= 1)
            {
                it = size_groups.erase(it);
            }
            else
            {
                ++it;
            }
        }

        SPDLOG_INFO("{} size groups with potential duplicates", size_groups.size());

        // Phase 3: Hash files within same-size groups
        size_t processed = 0;
        size_t total_to_hash = 0;
        for (const auto& [size, group_files] : size_groups)
        {
            total_to_hash += group_files.size();
        }

        std::unordered_map<std::string, DuplicateGroup> hash_groups;

        for (const auto& [size, group_files] : size_groups)
        {
            if (cancel_requested_.load()) break;

            for (const auto& file_path : group_files)
            {
                if (cancel_requested_.load()) break;

                std::string hash;
                try
                {
                    hash = CalculateHash(file_path, options.mode);
                }
                catch (const std::exception& e)
                {
                    SPDLOG_WARN("Failed to hash {}: {}", file_path.String(), e.what());
                    ++processed;
                    continue;
                }

                std::string key = std::to_string(size) + "_" + hash;
                
                if (hash_groups.find(key) == hash_groups.end())
                {
                    DuplicateGroup group;
                    group.hash = hash;
                    group.file_size = size;
                    hash_groups[key] = group;
                }

                hash_groups[key].files.push_back(file_path);
                ++processed;

                if (progress_callback)
                {
                    DuplicateProgress progress;
                    progress.files_scanned = processed;
                    progress.total_files = total_to_hash;
                    progress.current_file = file_path.Filename();
                    progress.current_phase = "Computing hashes";
                    progress.percentage = total_to_hash > 0 ?
                        (static_cast<double>(processed) / total_to_hash) * 100.0 : 0.0;
                    progress_callback(progress);
                }
            }
        }

        // Phase 4: Build result
        for (auto& [key, group] : hash_groups)
        {
            if (group.files.size() > 1)
            {
                // Calculate timestamps
                for (const auto& file_path : group.files)
                {
                    try
                    {
                        auto ftime = std::filesystem::last_write_time(file_path.Get());
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            ftime - std::filesystem::file_time_type::clock::now() + 
                            std::chrono::system_clock::now());
                        
                        if (group.oldest_modified == std::chrono::system_clock::time_point{} ||
                            sctp < group.oldest_modified)
                        {
                            group.oldest_modified = sctp;
                        }
                        if (sctp > group.newest_modified)
                        {
                            group.newest_modified = sctp;
                        }
                    }
                    catch (...) {}
                }

                result.groups.push_back(std::move(group));
                result.total_duplicates += group.files.size() - 1;
                result.total_wasted_space += group.GetWastedSpace();
            }
        }

        auto end_time = std::chrono::steady_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        result.success = !cancel_requested_.load();

        SPDLOG_INFO("Duplicate search complete: {} groups, {} duplicates, {} bytes wasted",
            result.groups.size(), result.total_duplicates, result.total_wasted_space);

        running_.store(false);

        std::lock_guard<std::mutex> lock(result_mutex_);
        current_result_ = result;

        return result;
    }

    void DuplicateFinder::FindDuplicatesAsync(
        const std::vector<core::Path>& paths,
        const DuplicateSearchOptions& options,
        DuplicateProgressCallback progress_callback,
        DuplicateCompleteCallback complete_callback)
    {
        if (running_.load())
        {
            if (complete_callback)
            {
                complete_callback(false, "Search already in progress");
            }
            return;
        }

        if (worker_thread_ && worker_thread_->joinable())
        {
            worker_thread_->join();
        }

        worker_thread_ = std::make_unique<std::thread>([this, paths, options,
                                                         progress_callback, complete_callback]()
        {
            auto result = FindDuplicates(paths, options, progress_callback);
            
            if (complete_callback)
            {
                complete_callback(result.success, result.error_message);
            }
        });
    }

    void DuplicateFinder::Cancel()
    {
        cancel_requested_.store(true);
    }

    DuplicateResult DuplicateFinder::GetCurrentResult() const
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        return current_result_;
    }

    std::vector<core::Path> DuplicateFinder::AutoSelect(
        const std::vector<DuplicateGroup>& groups,
        AutoSelectMode mode,
        const core::Path& folder_path)
    {
        std::vector<core::Path> selected;

        for (const auto& group : groups)
        {
            if (group.files.size() < 2) continue;

            core::Path keep;

            switch (mode)
            {
            case AutoSelectMode::KeepOldest:
                keep = group.GetOldestFile();
                break;
            case AutoSelectMode::KeepNewest:
                keep = group.GetNewestFile();
                break;
            case AutoSelectMode::KeepShortestPath:
                keep = group.GetShortestPath();
                break;
            case AutoSelectMode::KeepLongestPath:
                keep = group.GetLongestPath();
                break;
            case AutoSelectMode::KeepInFolder:
                for (const auto& file : group.files)
                {
                    if (file.String().find(folder_path.String()) == 0)
                    {
                        keep = file;
                        break;
                    }
                }
                if (keep.String().empty())
                {
                    keep = group.files[0];
                }
                break;
            case AutoSelectMode::KeepNotInFolder:
                for (const auto& file : group.files)
                {
                    if (file.String().find(folder_path.String()) != 0)
                    {
                        keep = file;
                        break;
                    }
                }
                if (keep.String().empty())
                {
                    keep = group.files[0];
                }
                break;
            }

            // Add all files except the one to keep
            for (const auto& file : group.files)
            {
                if (file.String() != keep.String())
                {
                    selected.push_back(file);
                }
            }
        }

        return selected;
    }

    size_t DuplicateFinder::DeleteFiles(
        const std::vector<core::Path>& files,
        bool use_recycle_bin)
    {
        size_t deleted = 0;

#ifdef _WIN32
        if (use_recycle_bin)
        {
            for (const auto& file : files)
            {
                std::wstring wide_path(file.String().begin(), file.String().end());
                wide_path.push_back(L'\0');  // Double null terminator
                wide_path.push_back(L'\0');

                SHFILEOPSTRUCTW op = {};
                op.wFunc = FO_DELETE;
                op.pFrom = wide_path.c_str();
                op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;

                if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted)
                {
                    ++deleted;
                }
            }
        }
        else
#endif
        {
            for (const auto& file : files)
            {
                try
                {
                    if (std::filesystem::remove(file.Get()))
                    {
                        ++deleted;
                    }
                }
                catch (const std::exception& e)
                {
                    SPDLOG_WARN("Failed to delete {}: {}", file.String(), e.what());
                }
            }
        }

        return deleted;
    }

    size_t DuplicateFinder::MoveFiles(
        const std::vector<core::Path>& files,
        const core::Path& destination)
    {
        size_t moved = 0;

        std::filesystem::create_directories(destination.Get());

        for (const auto& file : files)
        {
            try
            {
                core::Path dest_path(destination.String() + "/" + file.Filename());
                std::filesystem::rename(file.Get(), dest_path.Get());
                ++moved;
            }
            catch (const std::exception& e)
            {
                SPDLOG_WARN("Failed to move {}: {}", file.String(), e.what());
            }
        }

        return moved;
    }

    bool DuplicateFinder::CreateHardLinks(
        const DuplicateGroup& group,
        const core::Path& keep_file)
    {
#ifdef _WIN32
        for (const auto& file : group.files)
        {
            if (file.String() == keep_file.String()) continue;

            try
            {
                // Delete the duplicate
                std::filesystem::remove(file.Get());
                
                // Create hard link
                std::wstring link_path(file.String().begin(), file.String().end());
                std::wstring target_path(keep_file.String().begin(), keep_file.String().end());
                
                if (!CreateHardLinkW(link_path.c_str(), target_path.c_str(), nullptr))
                {
                    SPDLOG_ERROR("Failed to create hard link: {}", GetLastError());
                    return false;
                }
            }
            catch (const std::exception& e)
            {
                SPDLOG_ERROR("Failed to create hard link: {}", e.what());
                return false;
            }
        }
        return true;
#else
        return false;
#endif
    }

    std::string DuplicateFinder::ExportToCsv(const DuplicateResult& result) const
    {
        std::ostringstream ss;
        ss << "Group,Hash,Size,File Path,Modified\n";

        int group_num = 1;
        for (const auto& group : result.groups)
        {
            for (const auto& file : group.files)
            {
                ss << group_num << ",";
                ss << group.hash << ",";
                ss << group.file_size << ",";
                ss << "\"" << file.String() << "\",";
                
                try
                {
                    auto ftime = std::filesystem::last_write_time(file.Get());
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - std::filesystem::file_time_type::clock::now() + 
                        std::chrono::system_clock::now());
                    auto time = std::chrono::system_clock::to_time_t(sctp);
                    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
                }
                catch (...) {}
                
                ss << "\n";
            }
            ++group_num;
        }

        return ss.str();
    }

    std::string DuplicateFinder::ExportToHtml(const DuplicateResult& result) const
    {
        std::ostringstream ss;
        ss << R"(<!DOCTYPE html>
<html>
<head>
<title>Duplicate Files Report</title>
<style>
body { font-family: Arial, sans-serif; margin: 20px; }
.summary { background-color: #f5f5f5; padding: 15px; margin-bottom: 20px; }
.group { border: 1px solid #ddd; margin-bottom: 15px; }
.group-header { background-color: #4CAF50; color: white; padding: 10px; }
.file { padding: 8px; border-bottom: 1px solid #eee; }
.file:nth-child(odd) { background-color: #f9f9f9; }
.size { color: #666; }
</style>
</head>
<body>
<h1>Duplicate Files Report</h1>
<div class="summary">
<p><strong>Total Files Scanned:</strong> )" << result.total_files_scanned << R"(</p>
<p><strong>Duplicate Groups:</strong> )" << result.groups.size() << R"(</p>
<p><strong>Total Duplicates:</strong> )" << result.total_duplicates << R"(</p>
<p><strong>Wasted Space:</strong> )" << (result.total_wasted_space / (1024.0 * 1024.0)) << R"( MB</p>
<p><strong>Duration:</strong> )" << result.duration.count() << R"( ms</p>
</div>
)";

        int group_num = 1;
        for (const auto& group : result.groups)
        {
            ss << R"(<div class="group">)";
            ss << R"(<div class="group-header">Group )" << group_num;
            ss << " - " << group.files.size() << " files";
            ss << " (" << (group.file_size / 1024.0) << " KB each)";
            ss << R"(</div>)";

            for (const auto& file : group.files)
            {
                ss << R"(<div class="file">)" << file.String() << R"(</div>)";
            }

            ss << R"(</div>)";
            ++group_num;
        }

        ss << R"(</body></html>)";
        return ss.str();
    }

    std::vector<std::pair<core::Path, uint64_t>> DuplicateFinder::CollectFiles(
        const std::vector<core::Path>& paths,
        const DuplicateSearchOptions& options,
        DuplicateProgressCallback callback)
    {
        std::vector<std::pair<core::Path, uint64_t>> result;
        size_t scanned = 0;

        for (const auto& path : paths)
        {
            if (cancel_requested_.load()) break;

            if (!std::filesystem::exists(path.Get())) continue;

            auto iterator_options = std::filesystem::directory_options::skip_permission_denied;
            
            if (options.recursive)
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                    path.Get(), iterator_options))
                {
                    if (cancel_requested_.load()) break;
                    
                    if (!entry.is_regular_file()) continue;

                    std::string filename = entry.path().filename().string();
                    
                    // Skip hidden files if not included
                    if (!options.include_hidden && filename.front() == '.')
                    {
                        continue;
                    }

                    uint64_t size = 0;
                    try
                    {
                        size = entry.file_size();
                    }
                    catch (...) { continue; }

                    // Size filters
                    if (options.skip_zero_size && size == 0) continue;
                    if (options.min_size > 0 && size < options.min_size) continue;
                    if (options.max_size > 0 && size > options.max_size) continue;

                    // Extension filter
                    std::string ext = entry.path().extension().string();
                    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    if (!MatchesExtension(ext, options.include_extensions, options.exclude_extensions))
                    {
                        continue;
                    }

                    // Exclude patterns
                    if (MatchesExcludePatterns(entry.path().string(), options.exclude_patterns))
                    {
                        continue;
                    }

                    result.push_back({core::Path(entry.path().string()), size});
                    ++scanned;

                    if (callback && scanned % 100 == 0)
                    {
                        DuplicateProgress progress;
                        progress.files_scanned = scanned;
                        progress.current_file = filename;
                        progress.current_phase = "Scanning files";
                        callback(progress);
                    }
                }
            }
            else
            {
                for (const auto& entry : std::filesystem::directory_iterator(
                    path.Get(), iterator_options))
                {
                    if (cancel_requested_.load()) break;
                    if (!entry.is_regular_file()) continue;

                    uint64_t size = 0;
                    try
                    {
                        size = entry.file_size();
                    }
                    catch (...) { continue; }

                    if (options.skip_zero_size && size == 0) continue;

                    result.push_back({core::Path(entry.path().string()), size});
                    ++scanned;
                }
            }
        }

        return result;
    }

    std::unordered_map<uint64_t, std::vector<core::Path>> DuplicateFinder::GroupBySize(
        const std::vector<std::pair<core::Path, uint64_t>>& files)
    {
        std::unordered_map<uint64_t, std::vector<core::Path>> groups;
        
        for (const auto& [path, size] : files)
        {
            groups[size].push_back(path);
        }
        
        return groups;
    }

    std::string DuplicateFinder::CalculateHash(const core::Path& path, DuplicateMatchMode mode)
    {
        switch (mode)
        {
        case DuplicateMatchMode::SizeOnly:
        case DuplicateMatchMode::SizeAndName:
            return ""; // No hash needed

        case DuplicateMatchMode::QuickHash:
        case DuplicateMatchMode::SizeAndPartialHash:
            return CalculatePartialHash(path);

        case DuplicateMatchMode::ExactHash:
            {
                // Full file hash using FNV-1a
                std::ifstream file(path.String(), std::ios::binary);
                if (!file) return "";

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
        }

        return "";
    }

    std::string DuplicateFinder::CalculatePartialHash(const core::Path& path)
    {
        constexpr size_t BLOCK_SIZE = 64 * 1024; // 64KB

        std::ifstream file(path.String(), std::ios::binary | std::ios::ate);
        if (!file) return "";

        auto file_size = file.tellg();
        file.seekg(0);

        uint64_t hash = 14695981039346656037ULL;
        constexpr uint64_t prime = 1099511628211ULL;

        char buffer[BLOCK_SIZE];

        // Hash first block
        file.read(buffer, BLOCK_SIZE);
        for (std::streamsize i = 0; i < file.gcount(); ++i)
        {
            hash ^= static_cast<uint8_t>(buffer[i]);
            hash *= prime;
        }

        // Hash last block if file is large enough
        if (file_size > static_cast<std::streamoff>(BLOCK_SIZE * 2))
        {
            file.seekg(-static_cast<std::streamoff>(BLOCK_SIZE), std::ios::end);
            file.read(buffer, BLOCK_SIZE);
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

    bool DuplicateFinder::MatchesExtension(const std::string& ext,
                                            const std::vector<std::string>& include,
                                            const std::vector<std::string>& exclude) const
    {
        // Check exclude first
        for (const auto& ex : exclude)
        {
            std::string lower_ex = ex;
            std::transform(lower_ex.begin(), lower_ex.end(), lower_ex.begin(), ::tolower);
            if (lower_ex == ext) return false;
        }

        // If no include list, accept all
        if (include.empty()) return true;

        // Check include
        for (const auto& in : include)
        {
            std::string lower_in = in;
            std::transform(lower_in.begin(), lower_in.end(), lower_in.begin(), ::tolower);
            if (lower_in == ext) return true;
        }

        return false;
    }

    bool DuplicateFinder::MatchesExcludePatterns(const std::string& path,
                                                  const std::vector<std::string>& patterns) const
    {
        for (const auto& pattern : patterns)
        {
            try
            {
                std::regex rx(pattern, std::regex::icase);
                if (std::regex_search(path, rx))
                {
                    return true;
                }
            }
            catch (...) {}
        }
        return false;
    }

} // namespace opacity::batch
