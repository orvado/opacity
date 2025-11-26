#include "opacity/filesystem/NetworkStorage.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <codecvt>
#include <locale>
#include <mutex>
#include <regex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <WinNetWk.h>
#include <LM.h>
#include <WS2tcpip.h>
#pragma comment(lib, "mpr.lib")
#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace opacity::filesystem
{
    using namespace opacity::core;

    namespace {
        // Helper for wide string to narrow string conversion
        inline std::string WideToNarrow(const std::wstring& wide)
        {
            if (wide.empty()) return {};
            int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), 
                                           nullptr, 0, nullptr, nullptr);
            std::string result(size, 0);
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                               &result[0], size, nullptr, nullptr);
            return result;
        }
    }

    class NetworkStorage::Impl
    {
    public:
        int timeoutSeconds_ = 30;
        int retryCount_ = 3;
        bool cachingEnabled_ = true;
        
        NetworkDriveCallback driveCallback_;
        
        // FTP state (placeholder - would need libcurl or similar)
        bool ftpConnected_ = false;
        FtpConnectionInfo ftpInfo_;
        
        std::mutex mutex_;

#ifdef _WIN32
        NetworkDriveType DetectDriveType(const std::string& uncPath)
        {
            // Simple heuristics based on path
            std::string lower = uncPath;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (lower.find("webdav") != std::string::npos ||
                lower.find("davwwwroot") != std::string::npos) {
                return NetworkDriveType::WebDAV;
            }

            // Default to SMB for Windows network shares
            return NetworkDriveType::SMB;
        }

        NetworkDrive CreateDriveInfo(char letter, const std::string& remoteName)
        {
            NetworkDrive drive;
            drive.driveLetter = letter;
            drive.uncPath = remoteName;

            // Parse server and share
            std::string server, share, relative;
            if (ParseUNCPath(remoteName, server, share, relative)) {
                drive.serverName = server;
                drive.shareName = share;
                drive.displayName = "\\\\" + server + "\\" + share;
            } else {
                drive.displayName = remoteName;
            }

            drive.type = DetectDriveType(remoteName);
            drive.isConnected = true;

            // Get space info
            std::string drivePath = std::string(1, letter) + ":\\";
            ULARGE_INTEGER freeBytesAvailable, totalBytes, freeBytes;
            if (GetDiskFreeSpaceExA(drivePath.c_str(), &freeBytesAvailable, 
                                    &totalBytes, &freeBytes)) {
                drive.totalSpace = totalBytes.QuadPart;
                drive.freeSpace = freeBytes.QuadPart;
            }

            return drive;
        }

        static bool ParseUNCPath(const std::string& uncPath,
                                std::string& server,
                                std::string& share,
                                std::string& relativePath)
        {
            // Match \\server\share\optional\path
            std::regex uncRegex(R"(^\\\\([^\\]+)\\([^\\]+)(.*)$)");
            std::smatch match;

            if (std::regex_match(uncPath, match, uncRegex)) {
                server = match[1].str();
                share = match[2].str();
                relativePath = match[3].str();
                return true;
            }
            return false;
        }
#endif
    };

    // ============== NetworkStorage ==============

    NetworkStorage::NetworkStorage()
        : impl_(std::make_unique<Impl>())
    {}

    NetworkStorage::~NetworkStorage()
    {
        Shutdown();
    }

    NetworkStorage::NetworkStorage(NetworkStorage&&) noexcept = default;
    NetworkStorage& NetworkStorage::operator=(NetworkStorage&&) noexcept = default;

    bool NetworkStorage::Initialize()
    {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            Logger::Get()->error("NetworkStorage: WSAStartup failed");
            return false;
        }
#endif
        Logger::Get()->info("NetworkStorage: Initialized");
        return true;
    }

    void NetworkStorage::Shutdown()
    {
        DisconnectFtp();
#ifdef _WIN32
        WSACleanup();
#endif
        Logger::Get()->info("NetworkStorage: Shutdown");
    }

    std::vector<NetworkDrive> NetworkStorage::GetNetworkDrives()
    {
        std::vector<NetworkDrive> drives;

#ifdef _WIN32
        DWORD drivesMask = GetLogicalDrives();
        
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            if (drivesMask & (1 << (letter - 'A'))) {
                std::string drivePath = std::string(1, letter) + ":";
                
                if (GetDriveTypeA(drivePath.c_str()) == DRIVE_REMOTE) {
                    char remoteName[MAX_PATH] = {};
                    DWORD remoteNameLen = MAX_PATH;
                    
                    if (WNetGetConnectionA(drivePath.c_str(), remoteName, &remoteNameLen) == NO_ERROR) {
                        drives.push_back(impl_->CreateDriveInfo(letter, remoteName));
                    }
                }
            }
        }
#endif

        return drives;
    }

    std::optional<NetworkDrive> NetworkStorage::GetDriveInfo(char driveLetter)
    {
#ifdef _WIN32
        std::string drivePath = std::string(1, driveLetter) + ":";
        
        if (GetDriveTypeA(drivePath.c_str()) != DRIVE_REMOTE) {
            return std::nullopt;
        }

        char remoteName[MAX_PATH] = {};
        DWORD remoteNameLen = MAX_PATH;
        
        if (WNetGetConnectionA(drivePath.c_str(), remoteName, &remoteNameLen) == NO_ERROR) {
            return impl_->CreateDriveInfo(driveLetter, remoteName);
        }
#endif
        return std::nullopt;
    }

    std::optional<NetworkDrive> NetworkStorage::GetDriveFromUNC(const std::string& uncPath)
    {
        auto drives = GetNetworkDrives();
        
        for (const auto& drive : drives) {
            if (drive.uncPath == uncPath) {
                return drive;
            }
        }
        
        return std::nullopt;
    }

    NetworkResult NetworkStorage::ConnectDrive(const std::string& uncPath,
                                                char driveLetter,
                                                const ConnectionOptions& options)
    {
        NetworkResult result;

#ifdef _WIN32
        NETRESOURCEW nr = {};
        nr.dwType = RESOURCETYPE_DISK;
        
        std::wstring wUncPath(uncPath.begin(), uncPath.end());
        nr.lpRemoteName = const_cast<wchar_t*>(wUncPath.c_str());

        std::wstring localName;
        if (driveLetter != 0) {
            localName = std::wstring(1, driveLetter) + L":";
            nr.lpLocalName = const_cast<wchar_t*>(localName.c_str());
        }

        std::wstring wUsername, wPassword;
        LPCWSTR username = nullptr;
        LPCWSTR password = nullptr;

        if (!options.username.empty()) {
            wUsername = std::wstring(options.username.begin(), options.username.end());
            username = wUsername.c_str();
        }
        if (!options.password.empty()) {
            wPassword = std::wstring(options.password.begin(), options.password.end());
            password = wPassword.c_str();
        }

        DWORD flags = 0;
        if (options.reconnectAtLogon) flags |= CONNECT_UPDATE_PROFILE;
        if (options.interactive) flags |= CONNECT_INTERACTIVE;

        DWORD dwResult = WNetAddConnection2W(&nr, password, username, flags);

        if (dwResult == NO_ERROR) {
            result.success = true;
            Logger::Get()->info("NetworkStorage: Connected {} to {}", uncPath, 
                driveLetter ? std::string(1, driveLetter) + ":" : "");
            
            if (impl_->driveCallback_ && driveLetter) {
                auto driveInfo = GetDriveInfo(driveLetter);
                if (driveInfo) {
                    impl_->driveCallback_(*driveInfo, true);
                }
            }
        } else {
            result.errorCode = dwResult;
            
            char* msgBuf = nullptr;
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                nullptr, dwResult, 0, reinterpret_cast<LPSTR>(&msgBuf), 0, nullptr);
            
            if (msgBuf) {
                result.errorMessage = msgBuf;
                LocalFree(msgBuf);
            }
            
            Logger::Get()->error("NetworkStorage: Failed to connect {}: {}", 
                uncPath, result.errorMessage);
        }
#else
        result.errorMessage = "Not implemented for this platform";
#endif

        return result;
    }

    NetworkResult NetworkStorage::DisconnectDrive(char driveLetter, bool force)
    {
        NetworkResult result;

#ifdef _WIN32
        std::string localName = std::string(1, driveLetter) + ":";
        
        DWORD dwResult = WNetCancelConnectionA(localName.c_str(), force ? TRUE : FALSE);

        if (dwResult == NO_ERROR) {
            result.success = true;
            Logger::Get()->info("NetworkStorage: Disconnected {}", localName);
            
            if (impl_->driveCallback_) {
                NetworkDrive drive;
                drive.driveLetter = driveLetter;
                drive.isConnected = false;
                impl_->driveCallback_(drive, false);
            }
        } else {
            result.errorCode = dwResult;
            
            char* msgBuf = nullptr;
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                nullptr, dwResult, 0, reinterpret_cast<LPSTR>(&msgBuf), 0, nullptr);
            
            if (msgBuf) {
                result.errorMessage = msgBuf;
                LocalFree(msgBuf);
            }
        }
#else
        result.errorMessage = "Not implemented for this platform";
#endif

        return result;
    }

    NetworkResult NetworkStorage::DisconnectDrive(const std::string& uncPath, bool force)
    {
        NetworkResult result;

#ifdef _WIN32
        DWORD dwResult = WNetCancelConnectionA(uncPath.c_str(), force ? TRUE : FALSE);

        if (dwResult == NO_ERROR) {
            result.success = true;
            Logger::Get()->info("NetworkStorage: Disconnected {}", uncPath);
        } else {
            result.errorCode = dwResult;
            
            char* msgBuf = nullptr;
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                nullptr, dwResult, 0, reinterpret_cast<LPSTR>(&msgBuf), 0, nullptr);
            
            if (msgBuf) {
                result.errorMessage = msgBuf;
                LocalFree(msgBuf);
            }
        }
#else
        result.errorMessage = "Not implemented for this platform";
#endif

        return result;
    }

    bool NetworkStorage::IsDriveConnected(char driveLetter)
    {
#ifdef _WIN32
        std::string drivePath = std::string(1, driveLetter) + ":";
        return GetDriveTypeA(drivePath.c_str()) == DRIVE_REMOTE;
#else
        return false;
#endif
    }

    void NetworkStorage::ReconnectPersistentDrives()
    {
#ifdef _WIN32
        // Windows reconnects persistent drives automatically on logon
        // This forces a reconnection attempt
        auto drives = GetNetworkDrives();
        
        for (const auto& drive : drives) {
            if (!drive.isConnected && drive.driveLetter != 0) {
                // Try to reconnect
                std::string drivePath = std::string(1, drive.driveLetter) + ":\\";
                std::error_code ec;
                std::filesystem::exists(drivePath, ec);  // Triggers reconnection attempt
            }
        }
#endif
    }

    bool NetworkStorage::IsUNCPath(const std::filesystem::path& path)
    {
        std::string pathStr = path.string();
        return pathStr.length() >= 2 && pathStr[0] == '\\' && pathStr[1] == '\\';
    }

    bool NetworkStorage::ParseUNCPath(const std::string& uncPath,
                                       std::string& server,
                                       std::string& share,
                                       std::string& relativePath)
    {
#ifdef _WIN32
        return Impl::ParseUNCPath(uncPath, server, share, relativePath);
#else
        return false;
#endif
    }

    std::string NetworkStorage::DriveToUNC(const std::filesystem::path& path)
    {
#ifdef _WIN32
        std::string pathStr = path.string();
        
        if (pathStr.length() >= 2 && pathStr[1] == ':') {
            char driveLetter = static_cast<char>(std::toupper(pathStr[0]));
            std::string drivePath = std::string(1, driveLetter) + ":";
            
            if (GetDriveTypeA(drivePath.c_str()) == DRIVE_REMOTE) {
                char remoteName[MAX_PATH] = {};
                DWORD remoteNameLen = MAX_PATH;
                
                if (WNetGetConnectionA(drivePath.c_str(), remoteName, &remoteNameLen) == NO_ERROR) {
                    // Replace drive letter with UNC path
                    return std::string(remoteName) + pathStr.substr(2);
                }
            }
        }
#endif
        return path.string();
    }

    std::filesystem::path NetworkStorage::UNCToDrive(const std::string& uncPath)
    {
        auto drives = GetNetworkDrives();
        
        for (const auto& drive : drives) {
            if (uncPath.find(drive.uncPath) == 0) {
                // Replace UNC with drive letter
                std::string relativePart = uncPath.substr(drive.uncPath.length());
                return std::filesystem::path(std::string(1, drive.driveLetter) + ":" + relativePart);
            }
        }
        
        return std::filesystem::path(uncPath);
    }

    std::filesystem::path NetworkStorage::ResolvePath(const std::filesystem::path& path)
    {
        if (IsUNCPath(path)) {
            return UNCToDrive(path.string());
        }
        return path;
    }

    bool NetworkStorage::IsNetworkPath(const std::filesystem::path& path)
    {
        if (IsUNCPath(path)) {
            return true;
        }

#ifdef _WIN32
        std::string pathStr = path.string();
        if (pathStr.length() >= 2 && pathStr[1] == ':') {
            char driveLetter = static_cast<char>(std::toupper(pathStr[0]));
            std::string drivePath = std::string(1, driveLetter) + ":";
            return GetDriveTypeA(drivePath.c_str()) == DRIVE_REMOTE;
        }
#endif
        return false;
    }

    bool NetworkStorage::IsPathAvailable(const std::filesystem::path& path, int timeoutMs)
    {
        // Simple check - try to access the path
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    std::chrono::milliseconds NetworkStorage::GetPathLatency(const std::filesystem::path& path)
    {
        auto start = std::chrono::steady_clock::now();
        
        std::error_code ec;
        std::filesystem::exists(path, ec);
        
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }

    bool NetworkStorage::IsServerAvailable(const std::string& serverName, int timeoutMs)
    {
#ifdef _WIN32
        // Use socket connect to check server availability
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            return false;
        }

        // Set timeout
        DWORD timeout = timeoutMs;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));

        // Resolve hostname
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* result = nullptr;
        if (getaddrinfo(serverName.c_str(), "445", &hints, &result) != 0) {  // SMB port
            closesocket(sock);
            return false;
        }

        // Try to connect
        bool available = (connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) == 0);

        freeaddrinfo(result);
        closesocket(sock);

        return available;
#else
        return false;
#endif
    }

    std::pair<uint64_t, uint64_t> NetworkStorage::GetSpaceInfo(const std::filesystem::path& path)
    {
#ifdef _WIN32
        ULARGE_INTEGER freeBytesAvailable, totalBytes, freeBytes;
        
        if (GetDiskFreeSpaceExW(path.wstring().c_str(), &freeBytesAvailable, 
                                &totalBytes, &freeBytes)) {
            return {totalBytes.QuadPart, freeBytes.QuadPart};
        }
#endif
        return {0, 0};
    }

    std::vector<std::string> NetworkStorage::DiscoverServers(int timeoutMs)
    {
        std::vector<std::string> servers;

#ifdef _WIN32
        LPNETRESOURCEW lpnrLocal = nullptr;
        HANDLE hEnum;
        
        DWORD dwResult = WNetOpenEnumW(RESOURCE_GLOBALNET, RESOURCETYPE_DISK,
                                        0, lpnrLocal, &hEnum);
        
        if (dwResult != NO_ERROR) {
            return servers;
        }

        DWORD cbBuffer = 16384;
        auto lpnrBuffer = std::make_unique<BYTE[]>(cbBuffer);
        
        DWORD cEntries = static_cast<DWORD>(-1);
        
        while (true) {
            dwResult = WNetEnumResourceW(hEnum, &cEntries, lpnrBuffer.get(), &cbBuffer);
            
            if (dwResult == NO_ERROR) {
                auto* lpnr = reinterpret_cast<LPNETRESOURCEW>(lpnrBuffer.get());
                for (DWORD i = 0; i < cEntries; i++) {
                    if (lpnr[i].lpRemoteName) {
                        servers.push_back(WideToNarrow(lpnr[i].lpRemoteName));
                    }
                }
            } else if (dwResult == ERROR_NO_MORE_ITEMS) {
                break;
            } else {
                break;
            }
        }

        WNetCloseEnum(hEnum);
#endif

        return servers;
    }

    std::vector<std::string> NetworkStorage::GetServerShares(const std::string& serverName)
    {
        std::vector<std::string> shares;

#ifdef _WIN32
        int size = MultiByteToWideChar(CP_UTF8, 0, serverName.c_str(), -1, nullptr, 0);
        std::wstring wServerName(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, serverName.c_str(), -1, &wServerName[0], size);
        
        PSHARE_INFO_1 pBuf = nullptr;
        DWORD entriesRead = 0;
        DWORD totalEntries = 0;
        
        NET_API_STATUS status = NetShareEnum(
            const_cast<wchar_t*>(wServerName.c_str()),
            1,
            reinterpret_cast<LPBYTE*>(&pBuf),
            MAX_PREFERRED_LENGTH,
            &entriesRead,
            &totalEntries,
            nullptr
        );

        if (status == NERR_Success && pBuf) {
            for (DWORD i = 0; i < entriesRead; i++) {
                // Skip admin shares
                if (pBuf[i].shi1_type == STYPE_DISKTREE) {
                    shares.push_back(WideToNarrow(pBuf[i].shi1_netname));
                }
            }
            NetApiBufferFree(pBuf);
        }
#endif

        return shares;
    }

    // ============== FTP/SFTP Support (Placeholder) ==============

    NetworkResult NetworkStorage::ConnectFtp(const FtpConnectionInfo& info)
    {
        NetworkResult result;
        
        // Note: Full FTP implementation would require libcurl or similar
        // This is a placeholder
        impl_->ftpInfo_ = info;
        
        Logger::Get()->warn("NetworkStorage: FTP support not fully implemented");
        result.errorMessage = "FTP support requires libcurl (not included)";
        
        return result;
    }

    void NetworkStorage::DisconnectFtp()
    {
        impl_->ftpConnected_ = false;
    }

    bool NetworkStorage::IsFtpConnected() const
    {
        return impl_->ftpConnected_;
    }

    std::vector<RemoteEntry> NetworkStorage::ListFtpDirectory(const std::string& path)
    {
        return {};  // Placeholder
    }

    NetworkResult NetworkStorage::DownloadFtpFile(const std::string& remotePath,
                                                   const std::filesystem::path& localPath,
                                                   NetworkProgressCallback progress)
    {
        NetworkResult result;
        result.errorMessage = "FTP download not implemented";
        return result;
    }

    std::optional<RemoteEntry> NetworkStorage::GetFtpFileInfo(const std::string& path)
    {
        return std::nullopt;
    }

    std::string NetworkStorage::GetFtpCurrentDirectory()
    {
        return "/";
    }

    void NetworkStorage::SetTimeout(int timeoutSeconds)
    {
        impl_->timeoutSeconds_ = timeoutSeconds;
    }

    void NetworkStorage::SetCachingEnabled(bool enabled)
    {
        impl_->cachingEnabled_ = enabled;
    }

    void NetworkStorage::ClearCache()
    {
        // No caching implemented yet
    }

    void NetworkStorage::SetRetryCount(int count)
    {
        impl_->retryCount_ = count;
    }

    void NetworkStorage::OnDriveChanged(NetworkDriveCallback callback)
    {
        impl_->driveCallback_ = callback;
    }

} // namespace opacity::filesystem
