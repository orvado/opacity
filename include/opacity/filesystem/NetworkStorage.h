#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opacity::filesystem
{
    /**
     * @brief Network drive types
     */
    enum class NetworkDriveType
    {
        Unknown,
        SMB,            // Windows file sharing
        NFS,            // Network File System
        WebDAV,         // WebDAV
        FTP,            // FTP/FTPS
        SFTP,           // SSH File Transfer Protocol
        Cloud           // Cloud storage (OneDrive, etc.)
    };

    /**
     * @brief Network drive information
     */
    struct NetworkDrive
    {
        char driveLetter = 0;           // Drive letter (A-Z) or 0 for UNC
        std::string uncPath;            // UNC path (\\server\share)
        std::string displayName;
        std::string serverName;
        std::string shareName;
        NetworkDriveType type = NetworkDriveType::Unknown;
        bool isConnected = false;
        bool requiresAuth = false;
        std::string username;
        uint64_t totalSpace = 0;
        uint64_t freeSpace = 0;
    };

    /**
     * @brief Network operation result
     */
    struct NetworkResult
    {
        bool success = false;
        int errorCode = 0;
        std::string errorMessage;
    };

    /**
     * @brief Connection options
     */
    struct ConnectionOptions
    {
        std::string username;
        std::string password;
        std::string domain;
        int timeoutSeconds = 30;
        bool saveCredentials = false;
        bool reconnectAtLogon = false;
        bool interactive = false;       // Allow Windows auth dialog
    };

    /**
     * @brief FTP/SFTP connection info
     */
    struct FtpConnectionInfo
    {
        std::string host;
        int port = 21;
        std::string username;
        std::string password;
        bool useSftp = false;
        bool useFtps = false;
        std::string privateKeyPath;     // For SFTP
        bool passiveMode = true;
        int timeoutSeconds = 30;
    };

    /**
     * @brief Remote file entry (for FTP/SFTP)
     */
    struct RemoteEntry
    {
        std::string name;
        std::string path;
        bool isDirectory = false;
        uint64_t size = 0;
        std::chrono::system_clock::time_point modifiedTime;
        std::string permissions;
        std::string owner;
    };

    /**
     * @brief Progress callback for network operations
     */
    using NetworkProgressCallback = std::function<void(uint64_t transferred, uint64_t total)>;
    using NetworkDriveCallback = std::function<void(const NetworkDrive& drive, bool connected)>;

    /**
     * @brief Network storage manager
     * 
     * Provides:
     * - Network drive enumeration and management
     * - UNC path handling
     * - Network operation optimization
     * - Basic FTP/SFTP support (read-only)
     * - Connection management
     */
    class NetworkStorage
    {
    public:
        NetworkStorage();
        ~NetworkStorage();

        // Non-copyable, movable
        NetworkStorage(const NetworkStorage&) = delete;
        NetworkStorage& operator=(const NetworkStorage&) = delete;
        NetworkStorage(NetworkStorage&&) noexcept;
        NetworkStorage& operator=(NetworkStorage&&) noexcept;

        /**
         * @brief Initialize the network storage manager
         */
        bool Initialize();

        /**
         * @brief Shutdown and cleanup
         */
        void Shutdown();

        // ============== Network Drive Management ==============

        /**
         * @brief Get all network drives
         */
        std::vector<NetworkDrive> GetNetworkDrives();

        /**
         * @brief Get network drive info
         */
        std::optional<NetworkDrive> GetDriveInfo(char driveLetter);

        /**
         * @brief Get network drive from UNC path
         */
        std::optional<NetworkDrive> GetDriveFromUNC(const std::string& uncPath);

        /**
         * @brief Connect a network drive
         */
        NetworkResult ConnectDrive(const std::string& uncPath, 
                                   char driveLetter,
                                   const ConnectionOptions& options = {});

        /**
         * @brief Disconnect a network drive
         */
        NetworkResult DisconnectDrive(char driveLetter, bool force = false);

        /**
         * @brief Disconnect by UNC path
         */
        NetworkResult DisconnectDrive(const std::string& uncPath, bool force = false);

        /**
         * @brief Check if drive is connected
         */
        bool IsDriveConnected(char driveLetter);

        /**
         * @brief Reconnect all persistent drives
         */
        void ReconnectPersistentDrives();

        // ============== UNC Path Handling ==============

        /**
         * @brief Check if path is UNC path
         */
        static bool IsUNCPath(const std::filesystem::path& path);

        /**
         * @brief Parse UNC path components
         */
        static bool ParseUNCPath(const std::string& uncPath,
                                std::string& server,
                                std::string& share,
                                std::string& relativePath);

        /**
         * @brief Convert drive path to UNC path
         */
        std::string DriveToUNC(const std::filesystem::path& path);

        /**
         * @brief Convert UNC path to drive path if mapped
         */
        std::filesystem::path UNCToDrive(const std::string& uncPath);

        /**
         * @brief Resolve network path (handles both UNC and drive letters)
         */
        std::filesystem::path ResolvePath(const std::filesystem::path& path);

        // ============== Network Operations ==============

        /**
         * @brief Check if path is on network
         */
        bool IsNetworkPath(const std::filesystem::path& path);

        /**
         * @brief Check network path availability
         */
        bool IsPathAvailable(const std::filesystem::path& path, int timeoutMs = 5000);

        /**
         * @brief Get path latency (ping time)
         */
        std::chrono::milliseconds GetPathLatency(const std::filesystem::path& path);

        /**
         * @brief Check server availability
         */
        bool IsServerAvailable(const std::string& serverName, int timeoutMs = 5000);

        /**
         * @brief Get available space on network path
         */
        std::pair<uint64_t, uint64_t> GetSpaceInfo(const std::filesystem::path& path);

        // ============== Server Discovery ==============

        /**
         * @brief Discover servers on the network
         */
        std::vector<std::string> DiscoverServers(int timeoutMs = 5000);

        /**
         * @brief Get shares on a server
         */
        std::vector<std::string> GetServerShares(const std::string& serverName);

        // ============== FTP/SFTP Support (Read-Only) ==============

        /**
         * @brief Connect to FTP/SFTP server
         */
        NetworkResult ConnectFtp(const FtpConnectionInfo& info);

        /**
         * @brief Disconnect from FTP/SFTP server
         */
        void DisconnectFtp();

        /**
         * @brief Check if connected to FTP
         */
        bool IsFtpConnected() const;

        /**
         * @brief List FTP directory
         */
        std::vector<RemoteEntry> ListFtpDirectory(const std::string& path);

        /**
         * @brief Download file from FTP
         */
        NetworkResult DownloadFtpFile(const std::string& remotePath,
                                      const std::filesystem::path& localPath,
                                      NetworkProgressCallback progress = nullptr);

        /**
         * @brief Get FTP file info
         */
        std::optional<RemoteEntry> GetFtpFileInfo(const std::string& path);

        /**
         * @brief Get current FTP directory
         */
        std::string GetFtpCurrentDirectory();

        // ============== Optimization ==============

        /**
         * @brief Set operation timeout
         */
        void SetTimeout(int timeoutSeconds);

        /**
         * @brief Enable/disable caching for network operations
         */
        void SetCachingEnabled(bool enabled);

        /**
         * @brief Clear network caches
         */
        void ClearCache();

        /**
         * @brief Set retry count for failed operations
         */
        void SetRetryCount(int count);

        // ============== Callbacks ==============

        /**
         * @brief Set callback for drive connection changes
         */
        void OnDriveChanged(NetworkDriveCallback callback);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace opacity::filesystem
