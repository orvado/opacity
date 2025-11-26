#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opacity::filesystem
{
    // Supported cloud providers
    enum class CloudProvider
    {
        Unknown,
        OneDrive,
        OneDriveBusiness,
        Dropbox,
        GoogleDrive,
        iCloudDrive,
        Box,
        MEGA,
        NextCloud,
        OwnCloud
    };

    // Cloud file sync status
    enum class CloudSyncStatus
    {
        Unknown,
        Synced,           // File is fully synced locally
        SyncPending,      // File has local changes waiting to sync
        Syncing,          // File is currently syncing
        OnlineOnly,       // File is cloud-only (placeholder)
        AlwaysAvailable,  // File is always kept locally
        Error,            // Sync error occurred
        Paused            // Sync is paused
    };

    // Cloud provider information
    struct CloudProviderInfo
    {
        CloudProvider provider = CloudProvider::Unknown;
        std::string displayName;
        std::filesystem::path syncFolder;
        std::string accountName;
        std::string accountId;
        bool isSignedIn = false;
        bool isSyncing = false;
        uint64_t usedSpace = 0;
        uint64_t totalSpace = 0;
        uint64_t freeSpace = 0;
    };

    // Cloud file information
    struct CloudFileInfo
    {
        std::filesystem::path localPath;
        std::string cloudPath;
        CloudProvider provider = CloudProvider::Unknown;
        CloudSyncStatus status = CloudSyncStatus::Unknown;
        uint64_t localSize = 0;
        uint64_t cloudSize = 0;
        std::string shareLink;
        bool canShare = false;
        bool isShared = false;
    };

    // Callbacks
    using CloudStatusCallback = std::function<void(const std::filesystem::path&, CloudSyncStatus)>;
    using CloudProviderCallback = std::function<void(CloudProvider, bool connected)>;

    class CloudIntegration
    {
    public:
        CloudIntegration();
        ~CloudIntegration();

        // Non-copyable, movable
        CloudIntegration(const CloudIntegration&) = delete;
        CloudIntegration& operator=(const CloudIntegration&) = delete;
        CloudIntegration(CloudIntegration&&) noexcept;
        CloudIntegration& operator=(CloudIntegration&&) noexcept;

        // Initialization
        bool Initialize();
        void Shutdown();

        // Provider detection
        std::vector<CloudProviderInfo> DetectCloudProviders();
        std::optional<CloudProviderInfo> GetProviderInfo(CloudProvider provider);
        bool IsProviderInstalled(CloudProvider provider);
        bool IsProviderSignedIn(CloudProvider provider);

        // Cloud path detection
        bool IsCloudPath(const std::filesystem::path& path);
        std::optional<CloudProvider> GetCloudProvider(const std::filesystem::path& path);
        std::filesystem::path GetSyncFolder(CloudProvider provider);

        // Sync status
        CloudSyncStatus GetSyncStatus(const std::filesystem::path& path);
        CloudFileInfo GetCloudFileInfo(const std::filesystem::path& path);
        std::vector<CloudFileInfo> GetPendingSyncs(CloudProvider provider = CloudProvider::Unknown);

        // OneDrive specific (Windows has deep integration)
        bool IsOneDriveInstalled();
        std::filesystem::path GetOneDriveFolder();
        CloudSyncStatus GetOneDriveStatus(const std::filesystem::path& path);
        bool SetOneDriveKeepLocal(const std::filesystem::path& path);
        bool SetOneDriveFreeUpSpace(const std::filesystem::path& path);
        bool RequestOneDriveDownload(const std::filesystem::path& path);

        // Dropbox specific
        bool IsDropboxInstalled();
        std::filesystem::path GetDropboxFolder();

        // Google Drive specific
        bool IsGoogleDriveInstalled();
        std::filesystem::path GetGoogleDriveFolder();

        // File operations
        bool MakeAvailableOffline(const std::filesystem::path& path);
        bool MakeOnlineOnly(const std::filesystem::path& path);

        // Share links (provider-specific)
        std::optional<std::string> GetShareLink(const std::filesystem::path& path);
        bool CreateShareLink(const std::filesystem::path& path);
        bool RemoveShareLink(const std::filesystem::path& path);

        // Sync control
        void PauseSync(CloudProvider provider = CloudProvider::Unknown);
        void ResumeSync(CloudProvider provider = CloudProvider::Unknown);
        bool IsSyncPaused(CloudProvider provider);

        // Status icons
        std::string GetStatusIconName(CloudSyncStatus status);
        uint32_t GetStatusColor(CloudSyncStatus status);

        // Callbacks
        void OnStatusChanged(CloudStatusCallback callback);
        void OnProviderChanged(CloudProviderCallback callback);

        // Batch operations
        void RefreshAllStatuses();
        std::map<CloudProvider, std::vector<std::filesystem::path>> GroupByProvider(
            const std::vector<std::filesystem::path>& paths);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace opacity::filesystem
