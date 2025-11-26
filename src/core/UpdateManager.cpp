// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#include "opacity/core/UpdateManager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>
#include <regex>
#include <set>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <WinInet.h>
#include <wincrypt.h>
#include <ShlObj.h>
#include <shellapi.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace opacity { namespace core {

// Version implementation
std::string Version::toString() const {
    std::ostringstream oss;
    oss << major << "." << minor << "." << patch;
    if (!prerelease.empty()) {
        oss << "-" << prerelease;
    }
    if (!build.empty()) {
        oss << "+" << build;
    }
    return oss.str();
}

Version Version::parse(const std::string& versionString) {
    Version v;
    std::string s = versionString;
    
    // Remove leading 'v' if present
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
        s = s.substr(1);
    }
    
    // Parse build metadata
    size_t buildPos = s.find('+');
    if (buildPos != std::string::npos) {
        v.build = s.substr(buildPos + 1);
        s = s.substr(0, buildPos);
    }
    
    // Parse prerelease
    size_t prePos = s.find('-');
    if (prePos != std::string::npos) {
        v.prerelease = s.substr(prePos + 1);
        s = s.substr(0, prePos);
    }
    
    // Parse major.minor.patch
    std::istringstream iss(s);
    char dot;
    iss >> v.major >> dot >> v.minor >> dot >> v.patch;
    
    return v;
}

bool Version::operator<(const Version& other) const {
    if (major != other.major) return major < other.major;
    if (minor != other.minor) return minor < other.minor;
    if (patch != other.patch) return patch < other.patch;
    
    // Prerelease versions are less than release versions
    if (prerelease.empty() && !other.prerelease.empty()) return false;
    if (!prerelease.empty() && other.prerelease.empty()) return true;
    
    return prerelease < other.prerelease;
}

bool Version::operator>(const Version& other) const { return other < *this; }
bool Version::operator==(const Version& other) const { 
    return major == other.major && minor == other.minor && 
           patch == other.patch && prerelease == other.prerelease;
}
bool Version::operator!=(const Version& other) const { return !(*this == other); }
bool Version::operator<=(const Version& other) const { return !(other < *this); }
bool Version::operator>=(const Version& other) const { return !(*this < other); }

// Helper to convert timepoint to string
static std::string TimePointToString(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    localtime_s(&tm, &time);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

static std::chrono::system_clock::time_point StringToTimePoint(const std::string& str) {
    std::tm tm = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

class UpdateManager::Impl {
public:
    Version currentVersion;
    std::string updateUrl;
    std::string configPath;
    
    UpdateChannel channel = UpdateChannel::Stable;
    UpdateState state = UpdateState::Idle;
    UpdateInfo availableUpdate;
    bool updateAvailable = false;
    
    std::string downloadPath;
    std::string lastError;
    
    // Settings
    bool autoCheck = true;
    int autoCheckIntervalHours = 24;
    bool autoDownload = false;
    bool autoInstall = false;
    std::chrono::system_clock::time_point lastCheckTime;
    std::set<std::string> skippedVersions;
    bool installScheduled = false;
    
    // Download state
    std::atomic<bool> downloading{false};
    std::atomic<bool> cancelRequested{false};
    DownloadProgress downloadProgress;
    std::thread downloadThread;
    std::mutex downloadMutex;
    
    std::vector<EventCallback> callbacks;
    ProgressCallback progressCallback;
    
    bool initialized = false;
    
    void notifyEvent(UpdateEventType type, const std::string& msg = "") {
        UpdateEvent event;
        event.type = type;
        event.message = msg;
        event.progress = downloadProgress;
        
        for (auto& callback : callbacks) {
            try {
                callback(event);
            } catch (...) {}
        }
    }
    
    void notifyProgress() {
        if (progressCallback) {
            try {
                progressCallback(downloadProgress);
            } catch (...) {}
        }
        
        notifyEvent(UpdateEventType::DownloadProgress);
    }
    
    bool httpGet(const std::string& url, std::string& response) {
        HINTERNET hInternet = InternetOpenA("Opacity/1.0", 
                                           INTERNET_OPEN_TYPE_PRECONFIG, 
                                           nullptr, nullptr, 0);
        if (!hInternet) {
            lastError = "Failed to initialize WinINet";
            return false;
        }
        
        HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), 
                                             nullptr, 0, 
                                             INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE,
                                             0);
        if (!hConnect) {
            InternetCloseHandle(hInternet);
            lastError = "Failed to connect to URL";
            return false;
        }
        
        char buffer[4096];
        DWORD bytesRead;
        response.clear();
        
        while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            response.append(buffer, bytesRead);
        }
        
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        
        return true;
    }
    
    bool downloadFile(const std::string& url, const std::string& destPath) {
        HINTERNET hInternet = InternetOpenA("Opacity/1.0", 
                                           INTERNET_OPEN_TYPE_PRECONFIG, 
                                           nullptr, nullptr, 0);
        if (!hInternet) {
            return false;
        }
        
        HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), 
                                             nullptr, 0, 
                                             INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE,
                                             0);
        if (!hConnect) {
            InternetCloseHandle(hInternet);
            return false;
        }
        
        // Get content length
        DWORD contentLength = 0;
        DWORD bufSize = sizeof(contentLength);
        HttpQueryInfoA(hConnect, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                      &contentLength, &bufSize, nullptr);
        
        downloadProgress.totalBytes = contentLength;
        downloadProgress.bytesDownloaded = 0;
        
        std::ofstream file(destPath, std::ios::binary);
        if (!file) {
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return false;
        }
        
        char buffer[8192];
        DWORD bytesRead;
        auto startTime = std::chrono::steady_clock::now();
        
        while (!cancelRequested) {
            if (!InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) || bytesRead == 0) {
                break;
            }
            
            file.write(buffer, bytesRead);
            downloadProgress.bytesDownloaded += bytesRead;
            
            // Calculate progress
            if (downloadProgress.totalBytes > 0) {
                downloadProgress.percentage = 
                    100.0 * downloadProgress.bytesDownloaded / downloadProgress.totalBytes;
            }
            
            // Calculate speed
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed > 0) {
                downloadProgress.speedBytesPerSecond = 
                    static_cast<double>(downloadProgress.bytesDownloaded) / elapsed;
                
                if (downloadProgress.speedBytesPerSecond > 0 && downloadProgress.totalBytes > 0) {
                    uint64_t remaining = downloadProgress.totalBytes - downloadProgress.bytesDownloaded;
                    downloadProgress.estimatedSecondsRemaining = 
                        static_cast<int>(remaining / downloadProgress.speedBytesPerSecond);
                }
            }
            
            notifyProgress();
        }
        
        file.close();
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        
        if (cancelRequested) {
            fs::remove(destPath);
            return false;
        }
        
        return true;
    }
    
    std::string computeSHA256(const std::string& filePath) {
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        std::string result;
        
        if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            return "";
        }
        
        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptReleaseContext(hProv, 0);
            return "";
        }
        
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return "";
        }
        
        char buffer[8192];
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
            if (!CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer), 
                              static_cast<DWORD>(file.gcount()), 0)) {
                break;
            }
        }
        file.close();
        
        BYTE hash[32];
        DWORD hashLen = 32;
        if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
            std::ostringstream oss;
            for (DWORD i = 0; i < hashLen; i++) {
                oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
            }
            result = oss.str();
        }
        
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        
        return result;
    }
    
    bool saveToFile() {
        try {
            json j;
            j["autoCheck"] = autoCheck;
            j["autoCheckIntervalHours"] = autoCheckIntervalHours;
            j["autoDownload"] = autoDownload;
            j["autoInstall"] = autoInstall;
            j["channel"] = static_cast<int>(channel);
            j["lastCheckTime"] = TimePointToString(lastCheckTime);
            j["skippedVersions"] = std::vector<std::string>(skippedVersions.begin(), 
                                                            skippedVersions.end());
            j["installScheduled"] = installScheduled;
            j["downloadPath"] = downloadPath;
            
            fs::path dir = fs::path(configPath).parent_path();
            if (!fs::exists(dir)) {
                fs::create_directories(dir);
            }
            
            std::ofstream file(configPath);
            if (!file) {
                return false;
            }
            
            file << j.dump(2);
            return true;
        } catch (...) {
            return false;
        }
    }
    
    bool loadFromFile() {
        try {
            if (!fs::exists(configPath)) {
                return true;
            }
            
            std::ifstream file(configPath);
            if (!file) {
                return false;
            }
            
            json j = json::parse(file);
            
            if (j.contains("autoCheck")) autoCheck = j["autoCheck"];
            if (j.contains("autoCheckIntervalHours")) 
                autoCheckIntervalHours = j["autoCheckIntervalHours"];
            if (j.contains("autoDownload")) autoDownload = j["autoDownload"];
            if (j.contains("autoInstall")) autoInstall = j["autoInstall"];
            if (j.contains("channel")) channel = static_cast<UpdateChannel>(j["channel"].get<int>());
            if (j.contains("lastCheckTime")) 
                lastCheckTime = StringToTimePoint(j["lastCheckTime"]);
            if (j.contains("skippedVersions")) {
                auto versions = j["skippedVersions"].get<std::vector<std::string>>();
                skippedVersions.insert(versions.begin(), versions.end());
            }
            if (j.contains("installScheduled")) installScheduled = j["installScheduled"];
            if (j.contains("downloadPath")) downloadPath = j["downloadPath"];
            
            return true;
        } catch (...) {
            return false;
        }
    }
};

UpdateManager::UpdateManager() : pImpl(std::make_unique<Impl>()) {}

UpdateManager::~UpdateManager() {
    shutdown();
}

bool UpdateManager::initialize(const Version& currentVersion, const std::string& updateUrl) {
    if (pImpl->initialized) {
        return true;
    }
    
    pImpl->currentVersion = currentVersion;
    pImpl->updateUrl = updateUrl;
    
    // Set config path
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData))) {
        int len = WideCharToMultiByte(CP_UTF8, 0, appData, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string appDataStr(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, appData, -1, &appDataStr[0], len, nullptr, nullptr);
            pImpl->configPath = appDataStr + "\\Opacity\\update_settings.json";
        } else {
            pImpl->configPath = "update_settings.json";
        }
    } else {
        pImpl->configPath = "update_settings.json";
    }
    
    pImpl->loadFromFile();
    
    pImpl->initialized = true;
    spdlog::info("UpdateManager: initialized, current version {}", currentVersion.toString());
    return true;
}

void UpdateManager::shutdown() {
    if (pImpl->initialized) {
        pImpl->cancelRequested = true;
        if (pImpl->downloadThread.joinable()) {
            pImpl->downloadThread.join();
        }
        saveSettings();
        pImpl->initialized = false;
    }
}

Version UpdateManager::getCurrentVersion() const {
    return pImpl->currentVersion;
}

void UpdateManager::setCurrentVersion(const Version& version) {
    pImpl->currentVersion = version;
}

bool UpdateManager::checkForUpdates() {
    pImpl->state = UpdateState::Checking;
    pImpl->notifyEvent(UpdateEventType::CheckStarted);
    
    std::string response;
    if (!pImpl->httpGet(pImpl->updateUrl, response)) {
        pImpl->state = UpdateState::Failed;
        pImpl->notifyEvent(UpdateEventType::Error, pImpl->lastError);
        return false;
    }
    
    try {
        json releases = json::parse(response);
        
        for (const auto& release : releases) {
            // Skip prereleases if on stable channel
            bool isPrerelease = release.value("prerelease", false);
            if (pImpl->channel == UpdateChannel::Stable && isPrerelease) {
                continue;
            }
            
            std::string tagName = release.value("tag_name", "");
            Version releaseVersion = Version::parse(tagName);
            
            // Skip if older or same
            if (releaseVersion <= pImpl->currentVersion) {
                continue;
            }
            
            // Skip if version is skipped
            if (pImpl->skippedVersions.count(releaseVersion.toString())) {
                continue;
            }
            
            // Found a newer version
            pImpl->availableUpdate.version = releaseVersion;
            pImpl->availableUpdate.releaseNotes = release.value("body", "");
            pImpl->availableUpdate.isPrerelease = isPrerelease;
            
            // Parse release date
            std::string publishedAt = release.value("published_at", "");
            if (!publishedAt.empty()) {
                pImpl->availableUpdate.releaseDate = StringToTimePoint(publishedAt);
            }
            
            // Find download asset
            if (release.contains("assets")) {
                for (const auto& asset : release["assets"]) {
                    std::string name = asset.value("name", "");
                    // Look for Windows installer
                    if (name.find(".exe") != std::string::npos || 
                        name.find(".msi") != std::string::npos) {
                        pImpl->availableUpdate.downloadUrl = 
                            asset.value("browser_download_url", "");
                        pImpl->availableUpdate.fileName = name;
                        pImpl->availableUpdate.fileSize = asset.value("size", 0);
                        break;
                    }
                }
            }
            
            pImpl->updateAvailable = true;
            pImpl->state = UpdateState::Available;
            pImpl->lastCheckTime = std::chrono::system_clock::now();
            pImpl->saveToFile();
            
            pImpl->notifyEvent(UpdateEventType::UpdateAvailable, 
                              releaseVersion.toString());
            pImpl->notifyEvent(UpdateEventType::CheckCompleted);
            
            spdlog::info("UpdateManager: update available: {}", releaseVersion.toString());
            return true;
        }
        
        pImpl->updateAvailable = false;
        pImpl->state = UpdateState::UpToDate;
        pImpl->lastCheckTime = std::chrono::system_clock::now();
        pImpl->saveToFile();
        
        pImpl->notifyEvent(UpdateEventType::CheckCompleted);
        return false;
        
    } catch (const std::exception& e) {
        pImpl->lastError = e.what();
        pImpl->state = UpdateState::Failed;
        pImpl->notifyEvent(UpdateEventType::Error, pImpl->lastError);
        return false;
    }
}

bool UpdateManager::checkForUpdatesAsync() {
    std::thread([this]() {
        checkForUpdates();
    }).detach();
    return true;
}

bool UpdateManager::isUpdateAvailable() const {
    return pImpl->updateAvailable;
}

const UpdateInfo* UpdateManager::getAvailableUpdate() const {
    return pImpl->updateAvailable ? &pImpl->availableUpdate : nullptr;
}

void UpdateManager::setUpdateChannel(UpdateChannel channel) {
    pImpl->channel = channel;
    pImpl->saveToFile();
}

UpdateChannel UpdateManager::getUpdateChannel() const {
    return pImpl->channel;
}

bool UpdateManager::downloadUpdate() {
    if (!pImpl->updateAvailable || pImpl->availableUpdate.downloadUrl.empty()) {
        return false;
    }
    
    pImpl->state = UpdateState::Downloading;
    pImpl->downloading = true;
    pImpl->cancelRequested = false;
    pImpl->notifyEvent(UpdateEventType::DownloadStarted);
    
    // Create download directory
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    
    int len = WideCharToMultiByte(CP_UTF8, 0, tempPath, -1, nullptr, 0, nullptr, nullptr);
    std::string tempDir;
    if (len > 0) {
        tempDir.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, tempPath, -1, &tempDir[0], len, nullptr, nullptr);
    }
    
    pImpl->downloadPath = tempDir + "Opacity_Update_" + 
                         pImpl->availableUpdate.version.toString() + "_" +
                         pImpl->availableUpdate.fileName;
    
    bool success = pImpl->downloadFile(pImpl->availableUpdate.downloadUrl, 
                                       pImpl->downloadPath);
    
    pImpl->downloading = false;
    
    if (success) {
        pImpl->state = UpdateState::Downloaded;
        pImpl->notifyEvent(UpdateEventType::DownloadCompleted);
        pImpl->saveToFile();
        spdlog::info("UpdateManager: download completed: {}", pImpl->downloadPath);
        return true;
    } else {
        pImpl->state = UpdateState::Failed;
        pImpl->lastError = "Download failed or cancelled";
        pImpl->notifyEvent(UpdateEventType::Error, pImpl->lastError);
        return false;
    }
}

bool UpdateManager::downloadUpdateAsync() {
    if (pImpl->downloading) {
        return false;
    }
    
    if (pImpl->downloadThread.joinable()) {
        pImpl->downloadThread.join();
    }
    
    pImpl->downloadThread = std::thread([this]() {
        downloadUpdate();
    });
    
    return true;
}

bool UpdateManager::cancelDownload() {
    if (!pImpl->downloading) {
        return false;
    }
    
    pImpl->cancelRequested = true;
    return true;
}

bool UpdateManager::isDownloading() const {
    return pImpl->downloading;
}

DownloadProgress UpdateManager::getDownloadProgress() const {
    return pImpl->downloadProgress;
}

std::string UpdateManager::getDownloadedFilePath() const {
    return pImpl->downloadPath;
}

bool UpdateManager::installUpdate() {
    if (pImpl->downloadPath.empty() || !fs::exists(pImpl->downloadPath)) {
        pImpl->lastError = "No downloaded update found";
        return false;
    }
    
    pImpl->state = UpdateState::Installing;
    pImpl->notifyEvent(UpdateEventType::InstallStarted);
    
    // Launch installer
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "open";
    sei.lpFile = pImpl->downloadPath.c_str();
    sei.lpParameters = "/passive";  // Silent-ish install
    sei.nShow = SW_SHOWNORMAL;
    
    if (ShellExecuteExA(&sei)) {
        pImpl->notifyEvent(UpdateEventType::InstallCompleted);
        spdlog::info("UpdateManager: installer launched");
        return true;
    } else {
        pImpl->lastError = "Failed to launch installer";
        pImpl->state = UpdateState::Failed;
        pImpl->notifyEvent(UpdateEventType::Error, pImpl->lastError);
        return false;
    }
}

bool UpdateManager::installUpdateAndRestart() {
    if (installUpdate()) {
        // Request application exit
        PostQuitMessage(0);
        return true;
    }
    return false;
}

bool UpdateManager::scheduleInstallOnExit() {
    if (pImpl->downloadPath.empty() || !fs::exists(pImpl->downloadPath)) {
        return false;
    }
    
    pImpl->installScheduled = true;
    pImpl->saveToFile();
    return true;
}

bool UpdateManager::isInstallScheduled() const {
    return pImpl->installScheduled;
}

bool UpdateManager::verifyDownload() const {
    if (pImpl->downloadPath.empty() || !fs::exists(pImpl->downloadPath)) {
        return false;
    }
    
    if (pImpl->availableUpdate.sha256Hash.empty()) {
        // No hash to verify against
        return true;
    }
    
    std::string computedHash = pImpl->computeSHA256(pImpl->downloadPath);
    return computedHash == pImpl->availableUpdate.sha256Hash;
}

void UpdateManager::setAutoCheck(bool enabled) {
    pImpl->autoCheck = enabled;
    pImpl->saveToFile();
}

bool UpdateManager::isAutoCheckEnabled() const {
    return pImpl->autoCheck;
}

void UpdateManager::setAutoCheckInterval(int hours) {
    pImpl->autoCheckIntervalHours = hours;
    pImpl->saveToFile();
}

int UpdateManager::getAutoCheckInterval() const {
    return pImpl->autoCheckIntervalHours;
}

void UpdateManager::setAutoDownload(bool enabled) {
    pImpl->autoDownload = enabled;
    pImpl->saveToFile();
}

bool UpdateManager::isAutoDownloadEnabled() const {
    return pImpl->autoDownload;
}

void UpdateManager::setAutoInstall(bool enabled) {
    pImpl->autoInstall = enabled;
    pImpl->saveToFile();
}

bool UpdateManager::isAutoInstallEnabled() const {
    return pImpl->autoInstall;
}

std::chrono::system_clock::time_point UpdateManager::getLastCheckTime() const {
    return pImpl->lastCheckTime;
}

std::chrono::system_clock::time_point UpdateManager::getNextCheckTime() const {
    return pImpl->lastCheckTime + std::chrono::hours(pImpl->autoCheckIntervalHours);
}

void UpdateManager::skipVersion(const Version& version) {
    pImpl->skippedVersions.insert(version.toString());
    pImpl->saveToFile();
}

bool UpdateManager::isVersionSkipped(const Version& version) const {
    return pImpl->skippedVersions.count(version.toString()) > 0;
}

void UpdateManager::clearSkippedVersions() {
    pImpl->skippedVersions.clear();
    pImpl->saveToFile();
}

UpdateState UpdateManager::getState() const {
    return pImpl->state;
}

std::string UpdateManager::getStateMessage() const {
    switch (pImpl->state) {
        case UpdateState::Idle: return "Idle";
        case UpdateState::Checking: return "Checking for updates...";
        case UpdateState::Available: return "Update available";
        case UpdateState::Downloading: return "Downloading update...";
        case UpdateState::Downloaded: return "Update downloaded";
        case UpdateState::Installing: return "Installing update...";
        case UpdateState::RestartRequired: return "Restart required";
        case UpdateState::Failed: return "Update failed";
        case UpdateState::UpToDate: return "Up to date";
        default: return "Unknown";
    }
}

std::string UpdateManager::getLastError() const {
    return pImpl->lastError;
}

void UpdateManager::addEventCallback(EventCallback callback) {
    pImpl->callbacks.push_back(std::move(callback));
}

void UpdateManager::setProgressCallback(ProgressCallback callback) {
    pImpl->progressCallback = std::move(callback);
}

bool UpdateManager::canRollback() const {
    // Check if previous version backup exists
    return false; // Not implemented yet
}

bool UpdateManager::rollback() {
    return false; // Not implemented yet
}

Version UpdateManager::getPreviousVersion() const {
    return Version(); // Not implemented yet
}

std::string UpdateManager::getChangelog() const {
    return pImpl->availableUpdate.releaseNotes;
}

std::string UpdateManager::getChangelogHtml() const {
    return pImpl->availableUpdate.releaseNotesHtml;
}

bool UpdateManager::saveSettings() const {
    return pImpl->saveToFile();
}

bool UpdateManager::loadSettings() {
    return pImpl->loadFromFile();
}

}} // namespace opacity::core
