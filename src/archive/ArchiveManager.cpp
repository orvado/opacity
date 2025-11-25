#include "opacity/archive/ArchiveManager.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

// miniz for ZIP support
#include "miniz.h"
#include "miniz_zip.h"

namespace opacity::archive
{
    // ArchiveEntry implementation
    std::string ArchiveEntry::GetParent() const
    {
        auto pos = name.find_last_of("/\\");
        if (pos == std::string::npos) return "";
        return name.substr(0, pos);
    }

    std::string ArchiveEntry::GetFilename() const
    {
        auto pos = name.find_last_of("/\\");
        if (pos == std::string::npos) return name;
        return name.substr(pos + 1);
    }

    bool ArchiveEntry::IsRootLevel() const
    {
        auto trimmed = name;
        // Remove trailing slash for directories
        if (!trimmed.empty() && (trimmed.back() == '/' || trimmed.back() == '\\'))
        {
            trimmed.pop_back();
        }
        return trimmed.find_first_of("/\\") == std::string::npos;
    }

    // ArchiveManager implementation
    ArchiveManager::ArchiveManager() = default;
    ArchiveManager::~ArchiveManager()
    {
        Cancel();
    }

    bool ArchiveManager::IsArchive(const core::Path& path)
    {
        return GetFormat(path) != ArchiveFormat::Unknown;
    }

    ArchiveFormat ArchiveManager::GetFormat(const core::Path& path)
    {
        std::string ext = path.Extension();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".zip" || ext == ".zipx" || ext == ".jar" || ext == ".war" || ext == ".ear")
            return ArchiveFormat::Zip;
        if (ext == ".7z")
            return ArchiveFormat::SevenZip;
        if (ext == ".tar")
            return ArchiveFormat::Tar;
        if (ext == ".gz" || ext == ".tgz")
            return ArchiveFormat::TarGz;
        if (ext == ".bz2" || ext == ".tbz2")
            return ArchiveFormat::TarBz2;
        if (ext == ".rar")
            return ArchiveFormat::Rar;

        return ArchiveFormat::Unknown;
    }

    std::string ArchiveManager::GetExtension(ArchiveFormat format)
    {
        switch (format)
        {
        case ArchiveFormat::Zip: return ".zip";
        case ArchiveFormat::SevenZip: return ".7z";
        case ArchiveFormat::Tar: return ".tar";
        case ArchiveFormat::TarGz: return ".tar.gz";
        case ArchiveFormat::TarBz2: return ".tar.bz2";
        case ArchiveFormat::Rar: return ".rar";
        default: return "";
        }
    }

    ArchiveInfo ArchiveManager::GetArchiveInfo(const core::Path& path)
    {
        ArchiveInfo info;
        info.path = path;
        info.format = GetFormat(path);

        if (info.format == ArchiveFormat::Unknown)
        {
            last_error_ = "Unknown archive format";
            return info;
        }

        // Get file size
        try
        {
            info.archive_size = std::filesystem::file_size(path.Get());
            info.created_time = std::chrono::system_clock::now(); // Would need proper API
        }
        catch (const std::exception& e)
        {
            last_error_ = e.what();
            return info;
        }

        if (info.format == ArchiveFormat::Zip)
        {
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, path.String().c_str(), 0))
            {
                last_error_ = "Failed to open ZIP archive";
                return info;
            }

            info.file_count = mz_zip_reader_get_num_files(&zip);
            
            for (mz_uint i = 0; i < info.file_count; ++i)
            {
                mz_zip_archive_file_stat stat;
                if (mz_zip_reader_file_stat(&zip, i, &stat))
                {
                    info.total_uncompressed_size += stat.m_uncomp_size;
                    if (stat.m_is_directory)
                        ++info.directory_count;
                    if (stat.m_is_encrypted)
                        info.is_encrypted = true;
                }
            }

            // Adjust counts
            info.file_count -= info.directory_count;

            mz_zip_reader_end(&zip);
        }

        return info;
    }

    std::vector<ArchiveEntry> ArchiveManager::ListContents(
        const core::Path& path,
        const std::string& password)
    {
        std::vector<ArchiveEntry> entries;
        auto format = GetFormat(path);

        if (format != ArchiveFormat::Zip)
        {
            last_error_ = "Only ZIP format is currently supported";
            return entries;
        }

        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, path.String().c_str(), 0))
        {
            last_error_ = "Failed to open ZIP archive";
            return entries;
        }

        mz_uint num_files = mz_zip_reader_get_num_files(&zip);
        entries.reserve(num_files);

        for (mz_uint i = 0; i < num_files; ++i)
        {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&zip, i, &stat))
            {
                continue;
            }

            ArchiveEntry entry;
            entry.name = stat.m_filename;
            entry.full_path = stat.m_filename;
            entry.compressed_size = stat.m_comp_size;
            entry.uncompressed_size = stat.m_uncomp_size;
            entry.is_directory = stat.m_is_directory;
            entry.is_encrypted = stat.m_is_encrypted;
            entry.crc32 = stat.m_crc32;

            // Calculate compression ratio
            if (entry.uncompressed_size > 0)
            {
                entry.compression_ratio = 1.0 - 
                    (static_cast<double>(entry.compressed_size) / entry.uncompressed_size);
            }

            // Convert time
            std::time_t time = stat.m_time;
            entry.modified_time = std::chrono::system_clock::from_time_t(time);

            entries.push_back(entry);
        }

        mz_zip_reader_end(&zip);
        return entries;
    }

    std::vector<ArchiveEntry> ArchiveManager::ListDirectory(
        const core::Path& archive_path,
        const std::string& internal_path,
        const std::string& password)
    {
        auto all_entries = ListContents(archive_path, password);
        std::vector<ArchiveEntry> result;
        std::set<std::string> seen_dirs;

        std::string prefix = internal_path;
        if (!prefix.empty() && prefix.back() != '/' && prefix.back() != '\\')
        {
            prefix += '/';
        }

        for (const auto& entry : all_entries)
        {
            std::string name = entry.name;
            
            // Skip if not under the requested path
            if (!prefix.empty() && name.find(prefix) != 0)
            {
                continue;
            }

            // Get the relative path from the prefix
            std::string relative = name.substr(prefix.length());
            
            // Skip empty relative paths
            if (relative.empty() || relative == "/")
            {
                continue;
            }

            // Remove trailing slash for comparison
            std::string trimmed = relative;
            if (!trimmed.empty() && (trimmed.back() == '/' || trimmed.back() == '\\'))
            {
                trimmed.pop_back();
            }

            // Check if this is directly under the current level
            auto slash_pos = trimmed.find_first_of("/\\");
            if (slash_pos != std::string::npos)
            {
                // This is in a subdirectory - add the directory if not seen
                std::string dir_name = trimmed.substr(0, slash_pos);
                if (seen_dirs.find(dir_name) == seen_dirs.end())
                {
                    seen_dirs.insert(dir_name);
                    
                    ArchiveEntry dir_entry;
                    dir_entry.name = dir_name;
                    dir_entry.full_path = prefix + dir_name + "/";
                    dir_entry.is_directory = true;
                    result.push_back(dir_entry);
                }
            }
            else
            {
                // This is at the current level
                ArchiveEntry current_entry = entry;
                current_entry.name = trimmed;
                result.push_back(current_entry);
            }
        }

        // Sort: directories first, then by name
        std::sort(result.begin(), result.end(), [](const ArchiveEntry& a, const ArchiveEntry& b)
        {
            if (a.is_directory != b.is_directory)
                return a.is_directory > b.is_directory;
            return a.name < b.name;
        });

        return result;
    }

    std::vector<filesystem::FsItem> ArchiveManager::EntriesToFsItems(
        const std::vector<ArchiveEntry>& entries,
        const core::Path& archive_path)
    {
        std::vector<filesystem::FsItem> items;
        items.reserve(entries.size());

        for (const auto& entry : entries)
        {
            filesystem::FsItem item;
            item.name = entry.name;
            item.path = archive_path.String() + "!" + entry.full_path;
            item.size = entry.uncompressed_size;
            item.is_directory = entry.is_directory;
            item.modified_time = entry.modified_time;
            item.type = entry.is_directory ? filesystem::FileType::Directory : 
                        filesystem::DetermineFileType(entry.name);
            
            items.push_back(item);
        }

        return items;
    }

    ArchiveResult ArchiveManager::Extract(
        const core::Path& archive_path,
        const ExtractOptions& options,
        ArchiveProgressCallback progress_callback)
    {
        ArchiveResult result;
        auto format = GetFormat(archive_path);

        if (format != ArchiveFormat::Zip)
        {
            result.error_message = "Only ZIP format is currently supported for extraction";
            return result;
        }

        running_.store(true);
        cancel_requested_.store(false);

        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, archive_path.String().c_str(), 0))
        {
            result.error_message = "Failed to open ZIP archive";
            running_.store(false);
            return result;
        }

        mz_uint num_files = mz_zip_reader_get_num_files(&zip);
        
        // Calculate total size for progress
        uint64_t total_size = 0;
        for (mz_uint i = 0; i < num_files; ++i)
        {
            mz_zip_archive_file_stat stat;
            if (mz_zip_reader_file_stat(&zip, i, &stat))
            {
                total_size += stat.m_uncomp_size;
            }
        }

        // Create destination directory if needed
        std::filesystem::create_directories(options.destination.Get());

        for (mz_uint i = 0; i < num_files; ++i)
        {
            if (cancel_requested_.load())
            {
                result.error_message = "Extraction cancelled";
                break;
            }

            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&zip, i, &stat))
            {
                continue;
            }

            std::string entry_name = stat.m_filename;

            // Check if this file should be extracted
            if (!options.files.empty())
            {
                bool found = false;
                for (const auto& f : options.files)
                {
                    if (entry_name == f || entry_name.find(f + "/") == 0)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }

            // Determine output path
            core::Path output_path = options.preserve_paths ?
                core::Path(options.destination.String() + "/" + entry_name) :
                core::Path(options.destination.String() + "/" + 
                    entry_name.substr(entry_name.find_last_of("/\\") + 1));

            // Handle directories
            if (stat.m_is_directory)
            {
                std::filesystem::create_directories(output_path.Get());
                continue;
            }

            // Check for existing file
            if (std::filesystem::exists(output_path.Get()))
            {
                if (options.skip_existing)
                {
                    continue;
                }
                if (!options.overwrite_existing)
                {
                    result.failed_files.push_back(entry_name);
                    continue;
                }
            }

            // Create parent directories
            std::filesystem::create_directories(output_path.Parent().Get());

            // Extract file
            if (!mz_zip_reader_extract_to_file(&zip, i, output_path.String().c_str(), 0))
            {
                result.failed_files.push_back(entry_name);
                SPDLOG_WARN("Failed to extract: {}", entry_name);
                continue;
            }

            ++result.files_processed;
            result.bytes_processed += stat.m_uncomp_size;

            // Report progress
            if (progress_callback)
            {
                ArchiveProgress progress;
                progress.files_processed = result.files_processed;
                progress.total_files = num_files;
                progress.bytes_processed = result.bytes_processed;
                progress.total_bytes = total_size;
                progress.current_file = entry_name;
                progress.percentage = total_size > 0 ? 
                    (static_cast<double>(result.bytes_processed) / total_size) * 100.0 : 0.0;
                progress_callback(progress);
            }
        }

        mz_zip_reader_end(&zip);
        running_.store(false);

        result.success = result.failed_files.empty() && !cancel_requested_.load();
        return result;
    }

    bool ArchiveManager::ExtractFile(
        const core::Path& archive_path,
        const std::string& entry_name,
        const core::Path& destination,
        const std::string& password)
    {
        auto format = GetFormat(archive_path);
        if (format != ArchiveFormat::Zip)
        {
            last_error_ = "Only ZIP format is currently supported";
            return false;
        }

        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, archive_path.String().c_str(), 0))
        {
            last_error_ = "Failed to open ZIP archive";
            return false;
        }

        int file_index = mz_zip_reader_locate_file(&zip, entry_name.c_str(), nullptr, 0);
        if (file_index < 0)
        {
            mz_zip_reader_end(&zip);
            last_error_ = "File not found in archive: " + entry_name;
            return false;
        }

        // Create parent directories
        std::filesystem::create_directories(destination.Parent().Get());

        bool success = mz_zip_reader_extract_to_file(&zip, file_index, destination.String().c_str(), 0);
        
        mz_zip_reader_end(&zip);

        if (!success)
        {
            last_error_ = "Failed to extract file";
        }

        return success;
    }

    std::vector<uint8_t> ArchiveManager::ExtractToMemory(
        const core::Path& archive_path,
        const std::string& entry_name,
        const std::string& password)
    {
        std::vector<uint8_t> result;
        auto format = GetFormat(archive_path);

        if (format != ArchiveFormat::Zip)
        {
            last_error_ = "Only ZIP format is currently supported";
            return result;
        }

        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, archive_path.String().c_str(), 0))
        {
            last_error_ = "Failed to open ZIP archive";
            return result;
        }

        int file_index = mz_zip_reader_locate_file(&zip, entry_name.c_str(), nullptr, 0);
        if (file_index < 0)
        {
            mz_zip_reader_end(&zip);
            last_error_ = "File not found in archive: " + entry_name;
            return result;
        }

        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, file_index, &stat))
        {
            mz_zip_reader_end(&zip);
            last_error_ = "Failed to get file info";
            return result;
        }

        result.resize(static_cast<size_t>(stat.m_uncomp_size));
        
        if (!mz_zip_reader_extract_to_mem(&zip, file_index, result.data(), result.size(), 0))
        {
            mz_zip_reader_end(&zip);
            result.clear();
            last_error_ = "Failed to extract file to memory";
            return result;
        }

        mz_zip_reader_end(&zip);
        return result;
    }

    ArchiveResult ArchiveManager::Create(
        const core::Path& archive_path,
        const std::vector<core::Path>& source_paths,
        const CreateOptions& options,
        ArchiveProgressCallback progress_callback)
    {
        ArchiveResult result;

        if (options.format != ArchiveFormat::Zip)
        {
            result.error_message = "Only ZIP format is currently supported for creation";
            return result;
        }

        running_.store(true);
        cancel_requested_.store(false);

        // Collect all files to archive
        std::vector<std::pair<core::Path, std::string>> files_to_add;
        for (const auto& source : source_paths)
        {
            if (!std::filesystem::exists(source.Get()))
            {
                continue;
            }

            core::Path base_path = source.Parent();
            auto collected = CollectFiles(source, base_path, options);
            files_to_add.insert(files_to_add.end(), collected.begin(), collected.end());
        }

        if (files_to_add.empty())
        {
            result.error_message = "No files to archive";
            running_.store(false);
            return result;
        }

        // Calculate total size
        uint64_t total_size = 0;
        for (const auto& [path, _] : files_to_add)
        {
            try
            {
                if (std::filesystem::is_regular_file(path.Get()))
                {
                    total_size += std::filesystem::file_size(path.Get());
                }
            }
            catch (...) {}
        }

        // Create ZIP archive
        mz_zip_archive zip{};
        if (!mz_zip_writer_init_file(&zip, archive_path.String().c_str(), 0))
        {
            result.error_message = "Failed to create ZIP archive";
            running_.store(false);
            return result;
        }

        // Set compression level
        mz_uint flags = 0;
        switch (options.level)
        {
        case CompressionLevel::Store:
            flags = MZ_NO_COMPRESSION;
            break;
        case CompressionLevel::Fastest:
            flags = MZ_BEST_SPEED;
            break;
        case CompressionLevel::Maximum:
        case CompressionLevel::Ultra:
            flags = MZ_BEST_COMPRESSION;
            break;
        default:
            flags = MZ_DEFAULT_LEVEL;
            break;
        }

        size_t total_files = files_to_add.size();
        for (const auto& [file_path, archive_name] : files_to_add)
        {
            if (cancel_requested_.load())
            {
                result.error_message = "Creation cancelled";
                break;
            }

            if (std::filesystem::is_directory(file_path.Get()))
            {
                // Add directory entry
                std::string dir_name = archive_name + "/";
                mz_zip_writer_add_mem(&zip, dir_name.c_str(), nullptr, 0, flags);
            }
            else
            {
                // Add file
                if (!mz_zip_writer_add_file(&zip, archive_name.c_str(), 
                                            file_path.String().c_str(), nullptr, 0, flags))
                {
                    result.failed_files.push_back(file_path.String());
                    SPDLOG_WARN("Failed to add file to archive: {}", file_path.String());
                    continue;
                }

                try
                {
                    result.bytes_processed += std::filesystem::file_size(file_path.Get());
                }
                catch (...) {}
            }

            ++result.files_processed;

            // Report progress
            if (progress_callback)
            {
                ArchiveProgress progress;
                progress.files_processed = result.files_processed;
                progress.total_files = total_files;
                progress.bytes_processed = result.bytes_processed;
                progress.total_bytes = total_size;
                progress.current_file = archive_name;
                progress.percentage = total_files > 0 ? 
                    (static_cast<double>(result.files_processed) / total_files) * 100.0 : 0.0;
                progress_callback(progress);
            }
        }

        // Add comment if specified
        if (!options.comment.empty())
        {
            // Note: miniz doesn't have a direct API for archive comments
        }

        if (!mz_zip_writer_finalize_archive(&zip))
        {
            result.error_message = "Failed to finalize archive";
            mz_zip_writer_end(&zip);
            running_.store(false);
            return result;
        }

        mz_zip_writer_end(&zip);
        running_.store(false);

        result.success = result.failed_files.empty() && !cancel_requested_.load();
        return result;
    }

    ArchiveResult ArchiveManager::AddFiles(
        const core::Path& archive_path,
        const std::vector<core::Path>& source_paths,
        const core::Path& base_path,
        ArchiveProgressCallback progress_callback)
    {
        ArchiveResult result;
        result.error_message = "Adding files to existing archive not yet implemented";
        return result;
    }

    ArchiveResult ArchiveManager::DeleteFiles(
        const core::Path& archive_path,
        const std::vector<std::string>& entry_names)
    {
        ArchiveResult result;
        result.error_message = "Deleting files from archive not yet implemented";
        return result;
    }

    bool ArchiveManager::TestArchive(
        const core::Path& archive_path,
        const std::string& password)
    {
        auto format = GetFormat(archive_path);
        if (format != ArchiveFormat::Zip)
        {
            last_error_ = "Only ZIP format is currently supported";
            return false;
        }

        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, archive_path.String().c_str(), 0))
        {
            last_error_ = "Failed to open ZIP archive";
            return false;
        }

        mz_uint num_files = mz_zip_reader_get_num_files(&zip);
        bool valid = true;

        for (mz_uint i = 0; i < num_files && valid; ++i)
        {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&zip, i, &stat))
            {
                valid = false;
                break;
            }

            // Skip directories
            if (stat.m_is_directory)
            {
                continue;
            }

            // Verify file can be extracted (without actually extracting)
            size_t uncompressed_size = static_cast<size_t>(stat.m_uncomp_size);
            if (uncompressed_size > 0)
            {
                // Test by extracting to heap and computing CRC
                void* buffer = mz_zip_reader_extract_to_heap(&zip, i, &uncompressed_size, 0);
                if (!buffer)
                {
                    valid = false;
                }
                else
                {
                    mz_free(buffer);
                }
            }
        }

        mz_zip_reader_end(&zip);
        return valid;
    }

    void ArchiveManager::Cancel()
    {
        cancel_requested_.store(true);
    }

    std::vector<std::pair<core::Path, std::string>> ArchiveManager::CollectFiles(
        const core::Path& source,
        const core::Path& base_path,
        const CreateOptions& options)
    {
        std::vector<std::pair<core::Path, std::string>> result;

        if (!std::filesystem::exists(source.Get()))
        {
            return result;
        }

        std::string relative = std::filesystem::relative(source.Get(), 
                                                         base_path.Get()).string();

        // Check exclude patterns
        if (ShouldExclude(relative, options.exclude_patterns))
        {
            return result;
        }

        if (std::filesystem::is_directory(source.Get()))
        {
            if (options.include_root_folder)
            {
                result.push_back({source, relative});
            }

            if (options.recursive)
            {
                for (const auto& entry : std::filesystem::directory_iterator(source.Get()))
                {
                    auto sub_files = CollectFiles(core::Path(entry.path().string()), base_path, options);
                    result.insert(result.end(), sub_files.begin(), sub_files.end());
                }
            }
        }
        else
        {
            result.push_back({source, relative});
        }

        return result;
    }

    bool ArchiveManager::ShouldExclude(const std::string& path, 
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

} // namespace opacity::archive
