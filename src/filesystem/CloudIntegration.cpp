#include "opacity/filesystem/CloudIntegration.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <shellapi.h>
#endif

namespace opacity::filesystem
{
    using namespace opacity::core;

    class CloudIntegration::Impl
    {
    public:
        std::map<CloudProvider, CloudProviderInfo> detectedProviders_;
        CloudStatusCallback statusCallback_;
        CloudProviderCallback providerCallback_;
        bool initialized_ = false;
        std::mutex mutex_;

#ifdef _WIN32
        // Windows cloud files API state
        bool cloudFilesAvailable_ = false;

        std::filesystem::path GetKnownFolderPath(const KNOWNFOLDERID& folderId)
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &path))) {
                std::filesystem::path result(path);
                CoTaskMemFree(path);
                return result;
            }
            return {};
        }

        std::string ReadRegistryString(HKEY hKey, const std::string& subKey, 
                                       const std::string& valueName)
        {
            HKEY key;
            if (RegOpenKeyExA(hKey, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
                return "";
            }

            char buffer[MAX_PATH];
            DWORD size = sizeof(buffer);
            DWORD type;

            std::string result;
            if (RegQueryValueExA(key, valueName.c_str(), nullptr, &type, 
                                reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS) {
                if (type == REG_SZ || type == REG_EXPAND_SZ) {
                    result = buffer;
                }
            }

            RegCloseKey(key);
            return result;
        }

        std::filesystem::path DetectOneDriveFolder()
        {
            // Try registry first
            std::string userPath = ReadRegistryString(
                HKEY_CURRENT_USER,
                "Software\\Microsoft\\OneDrive",
                "UserFolder"
            );

            if (!userPath.empty() && std::filesystem::exists(userPath)) {
                return userPath;
            }

            // Try known folder
            auto knownPath = GetKnownFolderPath(FOLDERID_SkyDrive);
            if (!knownPath.empty() && std::filesystem::exists(knownPath)) {
                return knownPath;
            }

            // Try environment variable
            const char* envPath = std::getenv("OneDrive");
            if (envPath && std::filesystem::exists(envPath)) {
                return envPath;
            }

            // Try common locations
            std::filesystem::path userProfile = GetKnownFolderPath(FOLDERID_Profile);
            if (!userProfile.empty()) {
                auto defaultPath = userProfile / "OneDrive";
                if (std::filesystem::exists(defaultPath)) {
                    return defaultPath;
                }
            }

            return {};
        }

        std::filesystem::path DetectDropboxFolder()
        {
            // Dropbox stores its path in info.json
            std::filesystem::path appDataLocal = GetKnownFolderPath(FOLDERID_LocalAppData);
            
            std::filesystem::path infoJson = appDataLocal / "Dropbox" / "info.json";
            
            if (std::filesystem::exists(infoJson)) {
                std::ifstream file(infoJson);
                if (file.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());
                    
                    // Simple JSON parsing for path
                    size_t pathPos = content.find("\"path\"");
                    if (pathPos != std::string::npos) {
                        size_t valueStart = content.find("\"", pathPos + 7);
                        if (valueStart != std::string::npos) {
                            size_t valueEnd = content.find("\"", valueStart + 1);
                            if (valueEnd != std::string::npos) {
                                std::string path = content.substr(valueStart + 1, 
                                                                  valueEnd - valueStart - 1);
                                // Unescape
                                size_t pos;
                                while ((pos = path.find("\\\\")) != std::string::npos) {
                                    path.replace(pos, 2, "\\");
                                }
                                if (std::filesystem::exists(path)) {
                                    return path;
                                }
                            }
                        }
                    }
                }
            }

            // Try common locations
            std::filesystem::path userProfile = GetKnownFolderPath(FOLDERID_Profile);
            if (!userProfile.empty()) {
                auto defaultPath = userProfile / "Dropbox";
                if (std::filesystem::exists(defaultPath)) {
                    return defaultPath;
                }
            }

            return {};
        }

        std::filesystem::path DetectGoogleDriveFolder()
        {
            // Google Drive Desktop stores sync folder info in the registry
            std::string syncPath = ReadRegistryString(
                HKEY_CURRENT_USER,
                "Software\\Google\\DriveFS",
                "MountPoint"
            );

            if (!syncPath.empty() && std::filesystem::exists(syncPath)) {
                return syncPath;
            }

            // Check for Google Drive Backup & Sync (older app)
            std::filesystem::path userProfile = GetKnownFolderPath(FOLDERID_Profile);
            if (!userProfile.empty()) {
                auto defaultPath = userProfile / "Google Drive";
                if (std::filesystem::exists(defaultPath)) {
                    return defaultPath;
                }
            }

            // Check mounted drive (Google Drive streams)
            for (char letter = 'G'; letter <= 'Z'; ++letter) {
                std::filesystem::path drivePath = std::string(1, letter) + ":\\My Drive";
                if (std::filesystem::exists(drivePath)) {
                    return drivePath;
                }
            }

            return {};
        }

        std::filesystem::path DetectiCloudFolder()
        {
            std::filesystem::path userProfile = GetKnownFolderPath(FOLDERID_Profile);
            if (!userProfile.empty()) {
                auto icloudPath = userProfile / "iCloudDrive";
                if (std::filesystem::exists(icloudPath)) {
                    return icloudPath;
                }
            }
            return {};
        }

        // Get OneDrive file status using file attributes
        CloudSyncStatus GetOneDriveFileStatus(const std::filesystem::path& path)
        {
            DWORD attributes = GetFileAttributesW(path.wstring().c_str());
            
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                return CloudSyncStatus::Unknown;
            }

            // Cloud files API attributes (may not be defined in older SDKs)
            constexpr DWORD ATTR_RECALL_ON_OPEN = 0x00040000;
            constexpr DWORD ATTR_RECALL_ON_DATA_ACCESS = 0x00400000;
            constexpr DWORD ATTR_PINNED = 0x00080000;
            constexpr DWORD ATTR_UNPINNED = 0x00100000;

            // Check if it's a placeholder (online-only)
            if (attributes & ATTR_RECALL_ON_OPEN ||
                attributes & ATTR_RECALL_ON_DATA_ACCESS) {
                return CloudSyncStatus::OnlineOnly;
            }

            // Check pinned status
            if (attributes & ATTR_PINNED) {
                return CloudSyncStatus::AlwaysAvailable;
            }

            if (attributes & ATTR_UNPINNED) {
                return CloudSyncStatus::OnlineOnly;
            }

            // Check offline attribute
            if (attributes & FILE_ATTRIBUTE_OFFLINE) {
                return CloudSyncStatus::OnlineOnly;
            }

            // If reparse point but not offline, it might be syncing
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                return CloudSyncStatus::Syncing;
            }

            return CloudSyncStatus::Synced;
        }
#endif
    };

    // ============== CloudIntegration ==============

    CloudIntegration::CloudIntegration()
        : impl_(std::make_unique<Impl>())
    {}

    CloudIntegration::~CloudIntegration()
    {
        Shutdown();
    }

    CloudIntegration::CloudIntegration(CloudIntegration&&) noexcept = default;
    CloudIntegration& CloudIntegration::operator=(CloudIntegration&&) noexcept = default;

    bool CloudIntegration::Initialize()
    {
        impl_->initialized_ = true;
        
        // Detect available providers
        DetectCloudProviders();
        
        Logger::Get()->info("CloudIntegration: Initialized with {} providers detected",
            impl_->detectedProviders_.size());
        
        return true;
    }

    void CloudIntegration::Shutdown()
    {
        impl_->initialized_ = false;
        impl_->detectedProviders_.clear();
        Logger::Get()->info("CloudIntegration: Shutdown");
    }

    std::vector<CloudProviderInfo> CloudIntegration::DetectCloudProviders()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->detectedProviders_.clear();
        std::vector<CloudProviderInfo> providers;

#ifdef _WIN32
        // OneDrive
        auto oneDrivePath = impl_->DetectOneDriveFolder();
        if (!oneDrivePath.empty()) {
            CloudProviderInfo info;
            info.provider = CloudProvider::OneDrive;
            info.displayName = "OneDrive";
            info.syncFolder = oneDrivePath;
            info.isSignedIn = true;

            // Get account info from registry
            info.accountName = impl_->ReadRegistryString(
                HKEY_CURRENT_USER,
                "Software\\Microsoft\\OneDrive\\Accounts\\Personal",
                "UserEmail"
            );

            impl_->detectedProviders_[CloudProvider::OneDrive] = info;
            providers.push_back(info);
        }

        // OneDrive for Business
        auto oneDriveBizPath = impl_->ReadRegistryString(
            HKEY_CURRENT_USER,
            "Software\\Microsoft\\OneDrive\\Accounts\\Business1",
            "UserFolder"
        );
        if (!oneDriveBizPath.empty() && std::filesystem::exists(oneDriveBizPath)) {
            CloudProviderInfo info;
            info.provider = CloudProvider::OneDriveBusiness;
            info.displayName = "OneDrive for Business";
            info.syncFolder = oneDriveBizPath;
            info.isSignedIn = true;
            info.accountName = impl_->ReadRegistryString(
                HKEY_CURRENT_USER,
                "Software\\Microsoft\\OneDrive\\Accounts\\Business1",
                "UserEmail"
            );

            impl_->detectedProviders_[CloudProvider::OneDriveBusiness] = info;
            providers.push_back(info);
        }

        // Dropbox
        auto dropboxPath = impl_->DetectDropboxFolder();
        if (!dropboxPath.empty()) {
            CloudProviderInfo info;
            info.provider = CloudProvider::Dropbox;
            info.displayName = "Dropbox";
            info.syncFolder = dropboxPath;
            info.isSignedIn = true;

            impl_->detectedProviders_[CloudProvider::Dropbox] = info;
            providers.push_back(info);
        }

        // Google Drive
        auto googleDrivePath = impl_->DetectGoogleDriveFolder();
        if (!googleDrivePath.empty()) {
            CloudProviderInfo info;
            info.provider = CloudProvider::GoogleDrive;
            info.displayName = "Google Drive";
            info.syncFolder = googleDrivePath;
            info.isSignedIn = true;

            impl_->detectedProviders_[CloudProvider::GoogleDrive] = info;
            providers.push_back(info);
        }

        // iCloud Drive
        auto icloudPath = impl_->DetectiCloudFolder();
        if (!icloudPath.empty()) {
            CloudProviderInfo info;
            info.provider = CloudProvider::iCloudDrive;
            info.displayName = "iCloud Drive";
            info.syncFolder = icloudPath;
            info.isSignedIn = true;

            impl_->detectedProviders_[CloudProvider::iCloudDrive] = info;
            providers.push_back(info);
        }
#endif

        return providers;
    }

    std::optional<CloudProviderInfo> CloudIntegration::GetProviderInfo(CloudProvider provider)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        auto it = impl_->detectedProviders_.find(provider);
        if (it != impl_->detectedProviders_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool CloudIntegration::IsProviderInstalled(CloudProvider provider)
    {
        return impl_->detectedProviders_.find(provider) != impl_->detectedProviders_.end();
    }

    bool CloudIntegration::IsProviderSignedIn(CloudProvider provider)
    {
        auto info = GetProviderInfo(provider);
        return info && info->isSignedIn;
    }

    bool CloudIntegration::IsCloudPath(const std::filesystem::path& path)
    {
        return GetCloudProvider(path).has_value();
    }

    std::optional<CloudProvider> CloudIntegration::GetCloudProvider(const std::filesystem::path& path)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        std::string pathStr = path.string();
        std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(), ::tolower);

        for (const auto& [provider, info] : impl_->detectedProviders_) {
            std::string syncStr = info.syncFolder.string();
            std::transform(syncStr.begin(), syncStr.end(), syncStr.begin(), ::tolower);

            if (pathStr.find(syncStr) == 0) {
                return provider;
            }
        }

        return std::nullopt;
    }

    std::filesystem::path CloudIntegration::GetSyncFolder(CloudProvider provider)
    {
        auto info = GetProviderInfo(provider);
        return info ? info->syncFolder : std::filesystem::path{};
    }

    CloudSyncStatus CloudIntegration::GetSyncStatus(const std::filesystem::path& path)
    {
        auto provider = GetCloudProvider(path);
        if (!provider) {
            return CloudSyncStatus::Unknown;
        }

#ifdef _WIN32
        if (*provider == CloudProvider::OneDrive || 
            *provider == CloudProvider::OneDriveBusiness) {
            return impl_->GetOneDriveFileStatus(path);
        }
#endif

        // For other providers, check if file exists locally
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            return CloudSyncStatus::Synced;
        }

        return CloudSyncStatus::Unknown;
    }

    CloudFileInfo CloudIntegration::GetCloudFileInfo(const std::filesystem::path& path)
    {
        CloudFileInfo info;
        info.localPath = path;

        auto provider = GetCloudProvider(path);
        if (provider) {
            info.provider = *provider;
            
            auto providerInfo = GetProviderInfo(*provider);
            if (providerInfo) {
                // Calculate relative cloud path
                std::string relativePath = path.string().substr(
                    providerInfo->syncFolder.string().length());
                info.cloudPath = relativePath;
            }
        }

        info.status = GetSyncStatus(path);

        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            info.localSize = std::filesystem::file_size(path, ec);
        }

        return info;
    }

    std::vector<CloudFileInfo> CloudIntegration::GetPendingSyncs(CloudProvider provider)
    {
        // Would require monitoring file system for pending changes
        // Placeholder implementation
        return {};
    }

    // ============== OneDrive Specific ==============

    bool CloudIntegration::IsOneDriveInstalled()
    {
        return IsProviderInstalled(CloudProvider::OneDrive);
    }

    std::filesystem::path CloudIntegration::GetOneDriveFolder()
    {
        return GetSyncFolder(CloudProvider::OneDrive);
    }

    CloudSyncStatus CloudIntegration::GetOneDriveStatus(const std::filesystem::path& path)
    {
#ifdef _WIN32
        return impl_->GetOneDriveFileStatus(path);
#else
        return CloudSyncStatus::Unknown;
#endif
    }

    bool CloudIntegration::SetOneDriveKeepLocal(const std::filesystem::path& path)
    {
#ifdef _WIN32
        // Use SetFileAttributes to pin the file
        DWORD attributes = GetFileAttributesW(path.wstring().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            return false;
        }

        constexpr DWORD ATTR_PINNED = 0x00080000;
        constexpr DWORD ATTR_UNPINNED = 0x00100000;

        // Set pinned, clear unpinned
        attributes |= ATTR_PINNED;
        attributes &= ~ATTR_UNPINNED;

        return SetFileAttributesW(path.wstring().c_str(), attributes) != 0;
#else
        return false;
#endif
    }

    bool CloudIntegration::SetOneDriveFreeUpSpace(const std::filesystem::path& path)
    {
#ifdef _WIN32
        DWORD attributes = GetFileAttributesW(path.wstring().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            return false;
        }

        constexpr DWORD ATTR_PINNED = 0x00080000;
        constexpr DWORD ATTR_UNPINNED = 0x00100000;

        // Set unpinned, clear pinned
        attributes |= ATTR_UNPINNED;
        attributes &= ~ATTR_PINNED;

        return SetFileAttributesW(path.wstring().c_str(), attributes) != 0;
#else
        return false;
#endif
    }

    bool CloudIntegration::RequestOneDriveDownload(const std::filesystem::path& path)
    {
#ifdef _WIN32
        // Opening the file will trigger download
        HANDLE hFile = CreateFileW(
            path.wstring().c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            return true;
        }
        return false;
#else
        return false;
#endif
    }

    // ============== Dropbox Specific ==============

    bool CloudIntegration::IsDropboxInstalled()
    {
        return IsProviderInstalled(CloudProvider::Dropbox);
    }

    std::filesystem::path CloudIntegration::GetDropboxFolder()
    {
        return GetSyncFolder(CloudProvider::Dropbox);
    }

    // ============== Google Drive Specific ==============

    bool CloudIntegration::IsGoogleDriveInstalled()
    {
        return IsProviderInstalled(CloudProvider::GoogleDrive);
    }

    std::filesystem::path CloudIntegration::GetGoogleDriveFolder()
    {
        return GetSyncFolder(CloudProvider::GoogleDrive);
    }

    // ============== File Operations ==============

    bool CloudIntegration::MakeAvailableOffline(const std::filesystem::path& path)
    {
        auto provider = GetCloudProvider(path);
        if (!provider) return false;

        switch (*provider) {
            case CloudProvider::OneDrive:
            case CloudProvider::OneDriveBusiness:
                return SetOneDriveKeepLocal(path);
            default:
                // Other providers may not support this
                return false;
        }
    }

    bool CloudIntegration::MakeOnlineOnly(const std::filesystem::path& path)
    {
        auto provider = GetCloudProvider(path);
        if (!provider) return false;

        switch (*provider) {
            case CloudProvider::OneDrive:
            case CloudProvider::OneDriveBusiness:
                return SetOneDriveFreeUpSpace(path);
            default:
                return false;
        }
    }

    // ============== Share Links ==============

    std::optional<std::string> CloudIntegration::GetShareLink(const std::filesystem::path& path)
    {
        // Would require cloud provider API integration
        return std::nullopt;
    }

    bool CloudIntegration::CreateShareLink(const std::filesystem::path& path)
    {
        return false;  // Requires API integration
    }

    bool CloudIntegration::RemoveShareLink(const std::filesystem::path& path)
    {
        return false;  // Requires API integration
    }

    // ============== Sync Control ==============

    void CloudIntegration::PauseSync(CloudProvider provider)
    {
        Logger::Get()->warn("CloudIntegration: PauseSync not implemented");
    }

    void CloudIntegration::ResumeSync(CloudProvider provider)
    {
        Logger::Get()->warn("CloudIntegration: ResumeSync not implemented");
    }

    bool CloudIntegration::IsSyncPaused(CloudProvider provider)
    {
        return false;
    }

    // ============== Status Icons ==============

    std::string CloudIntegration::GetStatusIconName(CloudSyncStatus status)
    {
        switch (status) {
            case CloudSyncStatus::Synced:           return "synced";
            case CloudSyncStatus::SyncPending:      return "sync_pending";
            case CloudSyncStatus::Syncing:          return "syncing";
            case CloudSyncStatus::OnlineOnly:       return "cloud";
            case CloudSyncStatus::AlwaysAvailable:  return "pinned";
            case CloudSyncStatus::Error:            return "error";
            case CloudSyncStatus::Paused:           return "paused";
            default:                                return "unknown";
        }
    }

    uint32_t CloudIntegration::GetStatusColor(CloudSyncStatus status)
    {
        switch (status) {
            case CloudSyncStatus::Synced:           return 0xFF00AA00;  // Green
            case CloudSyncStatus::SyncPending:      return 0xFF00AAFF;  // Blue
            case CloudSyncStatus::Syncing:          return 0xFF00AAFF;  // Blue
            case CloudSyncStatus::OnlineOnly:       return 0xFF888888;  // Gray
            case CloudSyncStatus::AlwaysAvailable:  return 0xFF00AA00;  // Green
            case CloudSyncStatus::Error:            return 0xFFAA0000;  // Red
            case CloudSyncStatus::Paused:           return 0xFFAAAA00;  // Yellow
            default:                                return 0xFFAAAAAA;  // Light Gray
        }
    }

    // ============== Callbacks ==============

    void CloudIntegration::OnStatusChanged(CloudStatusCallback callback)
    {
        impl_->statusCallback_ = callback;
    }

    void CloudIntegration::OnProviderChanged(CloudProviderCallback callback)
    {
        impl_->providerCallback_ = callback;
    }

    // ============== Batch Operations ==============

    void CloudIntegration::RefreshAllStatuses()
    {
        DetectCloudProviders();
    }

    std::map<CloudProvider, std::vector<std::filesystem::path>> CloudIntegration::GroupByProvider(
        const std::vector<std::filesystem::path>& paths)
    {
        std::map<CloudProvider, std::vector<std::filesystem::path>> grouped;

        for (const auto& path : paths) {
            auto provider = GetCloudProvider(path);
            if (provider) {
                grouped[*provider].push_back(path);
            }
        }

        return grouped;
    }

} // namespace opacity::filesystem
