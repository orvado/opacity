#include "opacity/filesystem/FileSystemManager.h"
#include "opacity/core/Logger.h"

#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <codecvt>
#include <locale>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

namespace opacity::filesystem
{

namespace
{
    // Convert UTF-8 string to wide string
    std::wstring Utf8ToWide(const std::string& str)
    {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
        std::wstring result(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &result[0], size_needed);
        return result;
    }

    // Convert wide string to UTF-8 string
    std::string WideToUtf8(const std::wstring& wstr)
    {
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), &result[0], size_needed, nullptr, nullptr);
        return result;
    }

    // Get shell folder path
    std::string GetKnownFolderPath(const KNOWNFOLDERID& folder_id)
    {
        PWSTR path = nullptr;
        HRESULT hr = SHGetKnownFolderPath(folder_id, 0, nullptr, &path);
        if (SUCCEEDED(hr) && path)
        {
            std::string result = WideToUtf8(path);
            CoTaskMemFree(path);
            return result;
        }
        return "";
    }
}

FileSystemManager::FileSystemManager()
{
    core::Logger::Get()->debug("FileSystemManager initialized");
}

FileSystemManager::~FileSystemManager()
{
    core::Logger::Get()->debug("FileSystemManager destroyed");
}

DirectoryContents FileSystemManager::EnumerateDirectory(const core::Path& path, 
                                                         const EnumerationOptions& options)
{
    DirectoryContents result;
    result.success = false;

    std::wstring wide_path = Utf8ToWide(path.String());
    
    // Ensure path ends with backslash for search
    if (!wide_path.empty() && wide_path.back() != L'\\' && wide_path.back() != L'/')
    {
        wide_path += L'\\';
    }
    
    std::wstring search_path = wide_path + L"*";

    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(search_path.c_str(), &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        switch (error)
        {
        case ERROR_PATH_NOT_FOUND:
            result.error_message = "Path not found: " + path.String();
            break;
        case ERROR_ACCESS_DENIED:
            result.error_message = "Access denied: " + path.String();
            break;
        case ERROR_INVALID_NAME:
            result.error_message = "Invalid path name: " + path.String();
            break;
        default:
            result.error_message = "Failed to enumerate directory (error " + std::to_string(error) + "): " + path.String();
            break;
        }
        core::Logger::Get()->warn("Directory enumeration failed: {}", result.error_message);
        return result;
    }

    do
    {
        // Skip . and ..
        if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0)
        {
            continue;
        }

        bool is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool is_hidden = (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        bool is_system = (find_data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;

        // Apply filters
        if (!options.include_hidden && is_hidden)
            continue;
        if (!options.include_system && is_system)
            continue;
        if (!options.include_directories && is_directory)
            continue;
        if (!options.include_files && !is_directory)
            continue;

        FsItem item = CreateFsItemFromFindData(wide_path, &find_data);
        
        // Apply name filter pattern
        if (!options.filter_pattern.empty())
        {
            auto filtered = FsItemUtils::FilterByName({item}, options.filter_pattern);
            if (filtered.empty())
                continue;
        }

        result.items.push_back(std::move(item));

        if (is_directory)
        {
            result.total_directories++;
        }
        else
        {
            result.total_files++;
            result.total_size += (static_cast<uint64_t>(find_data.nFileSizeHigh) << 32) | find_data.nFileSizeLow;
        }

    } while (FindNextFileW(find_handle, &find_data));

    FindClose(find_handle);

    // Sort results
    FsItemComparator comparator(options.sort_column, options.sort_direction, options.folders_first);
    FsItemUtils::Sort(result.items, comparator);

    result.success = true;
    core::Logger::Get()->debug("Enumerated {} items in {}", result.items.size(), path.String());
    
    return result;
}

std::vector<DriveInfo> FileSystemManager::GetDrives()
{
    std::vector<DriveInfo> drives;
    
    DWORD drive_mask = GetLogicalDrives();
    if (drive_mask == 0)
    {
        core::Logger::Get()->warn("Failed to get logical drives");
        return drives;
    }

    char drive_letter = 'A';
    while (drive_mask)
    {
        if (drive_mask & 1)
        {
            DriveInfo info;
            info.drive_letter = std::string(1, drive_letter) + ":";
            
            std::string root_path = info.drive_letter + "\\";
            std::wstring wide_root = Utf8ToWide(root_path);
            
            UINT drive_type = GetDriveTypeW(wide_root.c_str());
            info.is_removable = (drive_type == DRIVE_REMOVABLE);
            info.is_network = (drive_type == DRIVE_REMOTE);
            
            // Check if drive is ready
            ULARGE_INTEGER free_bytes, total_bytes, available_bytes;
            if (GetDiskFreeSpaceExW(wide_root.c_str(), &available_bytes, &total_bytes, &free_bytes))
            {
                info.is_ready = true;
                info.total_bytes = total_bytes.QuadPart;
                info.free_bytes = free_bytes.QuadPart;
                info.available_bytes = available_bytes.QuadPart;
            }
            else
            {
                info.is_ready = false;
            }

            // Get volume information
            if (info.is_ready)
            {
                wchar_t volume_name[MAX_PATH + 1] = {0};
                wchar_t file_system[MAX_PATH + 1] = {0};
                
                if (GetVolumeInformationW(wide_root.c_str(), volume_name, MAX_PATH,
                                          nullptr, nullptr, nullptr, file_system, MAX_PATH))
                {
                    info.volume_name = WideToUtf8(volume_name);
                    info.file_system = WideToUtf8(file_system);
                }
            }

            drives.push_back(info);
        }
        
        drive_mask >>= 1;
        drive_letter++;
    }

    return drives;
}

std::string FileSystemManager::GetUserHomeDirectory()
{
    return GetKnownFolderPath(FOLDERID_Profile);
}

std::string FileSystemManager::GetUserDocumentsDirectory()
{
    return GetKnownFolderPath(FOLDERID_Documents);
}

std::string FileSystemManager::GetUserDesktopDirectory()
{
    return GetKnownFolderPath(FOLDERID_Desktop);
}

std::string FileSystemManager::GetUserDownloadsDirectory()
{
    return GetKnownFolderPath(FOLDERID_Downloads);
}

bool FileSystemManager::MakeDirectory(const core::Path& path)
{
    std::wstring wide_path = Utf8ToWide(path.String());
    
    // Use SHCreateDirectoryExW to create intermediate directories
    int result = SHCreateDirectoryExW(nullptr, wide_path.c_str(), nullptr);
    
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS)
    {
        core::Logger::Get()->debug("Created directory: {}", path.String());
        return true;
    }
    
    core::Logger::Get()->warn("Failed to create directory: {} (error {})", path.String(), result);
    return false;
}

bool FileSystemManager::Delete(const core::Path& path, bool recursive)
{
    std::wstring wide_path = Utf8ToWide(path.String());
    
    DWORD attributes = GetFileAttributesW(wide_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return false;  // Path doesn't exist
    }

    bool is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    
    if (is_directory)
    {
        if (recursive)
        {
            // Use shell operation for recursive delete
            // Double null-terminate the path for SHFILEOPSTRUCT
            std::wstring double_null_path = wide_path;
            double_null_path.push_back(L'\0');
            
            SHFILEOPSTRUCTW file_op = {0};
            file_op.wFunc = FO_DELETE;
            file_op.pFrom = double_null_path.c_str();
            file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            
            int result = SHFileOperationW(&file_op);
            if (result == 0 && !file_op.fAnyOperationsAborted)
            {
                core::Logger::Get()->debug("Deleted directory recursively: {}", path.String());
                return true;
            }
            return false;
        }
        else
        {
            if (RemoveDirectoryW(wide_path.c_str()))
            {
                core::Logger::Get()->debug("Deleted empty directory: {}", path.String());
                return true;
            }
            return false;
        }
    }
    else
    {
        if (DeleteFileW(wide_path.c_str()))
        {
            core::Logger::Get()->debug("Deleted file: {}", path.String());
            return true;
        }
        return false;
    }
}

bool FileSystemManager::Rename(const core::Path& old_path, const core::Path& new_path)
{
    std::wstring wide_old = Utf8ToWide(old_path.String());
    std::wstring wide_new = Utf8ToWide(new_path.String());
    
    if (MoveFileW(wide_old.c_str(), wide_new.c_str()))
    {
        core::Logger::Get()->debug("Renamed {} to {}", old_path.String(), new_path.String());
        return true;
    }
    
    core::Logger::Get()->warn("Failed to rename {} to {}", old_path.String(), new_path.String());
    return false;
}

bool FileSystemManager::Copy(const core::Path& source, const core::Path& dest, bool overwrite)
{
    std::wstring wide_source = Utf8ToWide(source.String());
    std::wstring wide_dest = Utf8ToWide(dest.String());
    
    DWORD attributes = GetFileAttributesW(wide_source.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }

    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        // Use shell operation for directory copy
        std::wstring double_null_source = wide_source;
        double_null_source.push_back(L'\0');
        std::wstring double_null_dest = wide_dest;
        double_null_dest.push_back(L'\0');
        
        SHFILEOPSTRUCTW file_op = {0};
        file_op.wFunc = FO_COPY;
        file_op.pFrom = double_null_source.c_str();
        file_op.pTo = double_null_dest.c_str();
        file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        if (!overwrite)
        {
            file_op.fFlags |= FOF_RENAMEONCOLLISION;
        }
        
        int result = SHFileOperationW(&file_op);
        return result == 0 && !file_op.fAnyOperationsAborted;
    }
    else
    {
        BOOL fail_if_exists = overwrite ? FALSE : TRUE;
        return CopyFileW(wide_source.c_str(), wide_dest.c_str(), fail_if_exists) != 0;
    }
}

bool FileSystemManager::Move(const core::Path& source, const core::Path& dest, bool overwrite)
{
    std::wstring wide_source = Utf8ToWide(source.String());
    std::wstring wide_dest = Utf8ToWide(dest.String());
    
    DWORD flags = MOVEFILE_COPY_ALLOWED;
    if (overwrite)
    {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    
    if (MoveFileExW(wide_source.c_str(), wide_dest.c_str(), flags))
    {
        core::Logger::Get()->debug("Moved {} to {}", source.String(), dest.String());
        return true;
    }
    
    return false;
}

std::optional<FsItem> FileSystemManager::GetFileInfo(const core::Path& path)
{
    std::wstring wide_path = Utf8ToWide(path.String());
    
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(wide_path.c_str(), &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }
    
    // Extract directory path
    std::wstring dir_path = wide_path;
    size_t last_sep = dir_path.find_last_of(L"\\/");
    if (last_sep != std::wstring::npos)
    {
        dir_path = dir_path.substr(0, last_sep + 1);
    }
    
    FsItem item = CreateFsItemFromFindData(dir_path, &find_data);
    FindClose(find_handle);
    
    return item;
}

bool FileSystemManager::Exists(const core::Path& path)
{
    std::wstring wide_path = Utf8ToWide(path.String());
    DWORD attributes = GetFileAttributesW(wide_path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES;
}

bool FileSystemManager::IsDirectory(const core::Path& path)
{
    std::wstring wide_path = Utf8ToWide(path.String());
    DWORD attributes = GetFileAttributesW(wide_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool FileSystemManager::IsFile(const core::Path& path)
{
    std::wstring wide_path = Utf8ToWide(path.String());
    DWORD attributes = GetFileAttributesW(wide_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

core::Path FileSystemManager::GetParentDirectory(const core::Path& path)
{
    std::string path_str = path.String();
    
    // Remove trailing slashes
    while (!path_str.empty() && (path_str.back() == '\\' || path_str.back() == '/'))
    {
        path_str.pop_back();
    }
    
    size_t last_sep = path_str.find_last_of("\\/");
    if (last_sep == std::string::npos)
    {
        return core::Path(std::string(""));
    }
    
    // Handle root paths like "C:\"
    if (last_sep <= 2 && path_str.length() >= 2 && path_str[1] == ':')
    {
        return core::Path(path_str.substr(0, last_sep + 1));
    }
    
    return core::Path(path_str.substr(0, last_sep));
}

core::Path FileSystemManager::NormalizePath(const core::Path& path)
{
    std::wstring wide_path = Utf8ToWide(path.String());
    
    wchar_t normalized[MAX_PATH];
    if (PathCanonicalizeW(normalized, wide_path.c_str()))
    {
        return core::Path(WideToUtf8(normalized));
    }
    
    return path;
}

std::chrono::system_clock::time_point FileSystemManager::FileTimeToTimePoint(uint64_t file_time)
{
    // Windows FILETIME is 100-nanosecond intervals since January 1, 1601
    // Convert to Unix epoch (January 1, 1970)
    
    // Difference between 1601 and 1970 in 100-nanosecond intervals
    constexpr uint64_t EPOCH_DIFFERENCE = 116444736000000000ULL;
    
    if (file_time < EPOCH_DIFFERENCE)
    {
        return std::chrono::system_clock::time_point();
    }
    
    uint64_t unix_time_100ns = file_time - EPOCH_DIFFERENCE;
    
    // Convert to microseconds (more portable than nanoseconds)
    auto duration = std::chrono::microseconds(unix_time_100ns / 10);
    
    return std::chrono::system_clock::time_point(duration);
}

FsItem FileSystemManager::CreateFsItemFromFindData(const std::wstring& directory_path, void* find_data_ptr)
{
    WIN32_FIND_DATAW* find_data = static_cast<WIN32_FIND_DATAW*>(find_data_ptr);
    
    FsItem item;
    item.name = WideToUtf8(find_data->cFileName);
    item.full_path = core::Path(WideToUtf8(directory_path + find_data->cFileName));
    item.is_directory = (find_data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    item.is_symlink = (find_data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    item.size = (static_cast<uint64_t>(find_data->nFileSizeHigh) << 32) | find_data->nFileSizeLow;
    item.attributes = find_data->dwFileAttributes;
    
    // Convert FILETIME to time_point
    uint64_t modified_ft = (static_cast<uint64_t>(find_data->ftLastWriteTime.dwHighDateTime) << 32) | 
                           find_data->ftLastWriteTime.dwLowDateTime;
    uint64_t created_ft = (static_cast<uint64_t>(find_data->ftCreationTime.dwHighDateTime) << 32) | 
                          find_data->ftCreationTime.dwLowDateTime;
    
    item.modified = FileTimeToTimePoint(modified_ft);
    item.created = FileTimeToTimePoint(created_ft);
    
    // Extract extension
    if (!item.is_directory)
    {
        item.extension = FsItemUtils::GetExtension(item.name);
        item.mime_type = FsItemUtils::GetMimeType(item.extension);
    }
    
    return item;
}

} // namespace opacity::filesystem
