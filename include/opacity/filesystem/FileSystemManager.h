#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include "opacity/filesystem/FsItem.h"
#include "opacity/core/Path.h"

namespace opacity::filesystem
{

    /**
     * @brief Result of a directory enumeration operation
     */
    struct DirectoryContents
    {
        std::vector<FsItem> items;
        std::string error_message;
        bool success = false;
        size_t total_files = 0;
        size_t total_directories = 0;
        uint64_t total_size = 0;
    };

    /**
     * @brief Options for directory enumeration
     */
    struct EnumerationOptions
    {
        bool include_hidden = false;
        bool include_system = false;
        bool include_files = true;
        bool include_directories = true;
        bool follow_symlinks = true;
        SortColumn sort_column = SortColumn::Name;
        SortDirection sort_direction = SortDirection::Ascending;
        bool folders_first = true;
        std::string filter_pattern = "";  // Wildcard pattern for filtering
    };

    /**
     * @brief Drive information for Windows
     */
    struct DriveInfo
    {
        std::string drive_letter;       // e.g., "C:"
        std::string volume_name;        // e.g., "Windows"
        std::string file_system;        // e.g., "NTFS"
        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
        uint64_t available_bytes = 0;
        bool is_removable = false;
        bool is_network = false;
        bool is_ready = false;
    };

    /**
     * @brief Manages filesystem operations
     */
    class FileSystemManager
    {
    public:
        FileSystemManager();
        ~FileSystemManager();

        // Disable copy
        FileSystemManager(const FileSystemManager&) = delete;
        FileSystemManager& operator=(const FileSystemManager&) = delete;

        // Directory operations
        DirectoryContents EnumerateDirectory(const core::Path& path, 
                                             const EnumerationOptions& options = EnumerationOptions());
        
        // Get list of available drives
        std::vector<DriveInfo> GetDrives();
        
        // Get special folder paths
        std::string GetUserHomeDirectory();
        std::string GetUserDocumentsDirectory();
        std::string GetUserDesktopDirectory();
        std::string GetUserDownloadsDirectory();
        
        // File/Directory operations
        bool MakeDirectory(const core::Path& path);
        bool Delete(const core::Path& path, bool recursive = false);
        bool Rename(const core::Path& old_path, const core::Path& new_path);
        bool Copy(const core::Path& source, const core::Path& dest, bool overwrite = false);
        bool Move(const core::Path& source, const core::Path& dest, bool overwrite = false);
        
        // File information
        std::optional<FsItem> GetFileInfo(const core::Path& path);
        bool Exists(const core::Path& path);
        bool IsDirectory(const core::Path& path);
        bool IsFile(const core::Path& path);
        
        // Get parent directory
        core::Path GetParentDirectory(const core::Path& path);
        
        // Normalize path (resolve . and .., convert slashes)
        core::Path NormalizePath(const core::Path& path);

    private:
        // Convert Windows FILETIME to system_clock time_point
        std::chrono::system_clock::time_point FileTimeToTimePoint(uint64_t file_time);
        
        // Create FsItem from WIN32_FIND_DATA
        FsItem CreateFsItemFromFindData(const std::wstring& directory_path, void* find_data);
    };

} // namespace opacity::filesystem
