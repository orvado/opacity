// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#ifndef OPACITY_CORE_UPDATE_MANAGER_H
#define OPACITY_CORE_UPDATE_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <memory>

namespace opacity { namespace core {

/**
 * @brief Version information
 */
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;     // "alpha", "beta", "rc1", etc.
    std::string build;          // Build metadata
    
    Version() = default;
    Version(int maj, int min, int pat) : major(maj), minor(min), patch(pat) {}
    
    std::string toString() const;
    static Version parse(const std::string& versionString);
    
    bool operator<(const Version& other) const;
    bool operator>(const Version& other) const;
    bool operator==(const Version& other) const;
    bool operator!=(const Version& other) const;
    bool operator<=(const Version& other) const;
    bool operator>=(const Version& other) const;
};

/**
 * @brief Update channel
 */
enum class UpdateChannel {
    Stable,     // Stable releases only
    Beta,       // Beta and stable releases
    Nightly     // All releases including nightly builds
};

/**
 * @brief Update information
 */
struct UpdateInfo {
    Version version;
    std::string releaseNotes;
    std::string releaseNotesHtml;
    std::string downloadUrl;
    std::string fileName;
    uint64_t fileSize = 0;
    std::string sha256Hash;
    std::chrono::system_clock::time_point releaseDate;
    bool isPrerelease = false;
    bool isCritical = false;        // Critical security update
    std::vector<std::string> changes;
    std::string minimumOSVersion;
};

/**
 * @brief Download progress
 */
struct DownloadProgress {
    uint64_t bytesDownloaded = 0;
    uint64_t totalBytes = 0;
    double percentage = 0.0;
    double speedBytesPerSecond = 0.0;
    int estimatedSecondsRemaining = 0;
    std::string status;
};

/**
 * @brief Update state
 */
enum class UpdateState {
    Idle,
    Checking,
    Available,
    Downloading,
    Downloaded,
    Installing,
    RestartRequired,
    Failed,
    UpToDate
};

/**
 * @brief Update event types
 */
enum class UpdateEventType {
    CheckStarted,
    CheckCompleted,
    UpdateAvailable,
    DownloadStarted,
    DownloadProgress,
    DownloadCompleted,
    InstallStarted,
    InstallCompleted,
    Error
};

/**
 * @brief Update event data
 */
struct UpdateEvent {
    UpdateEventType type;
    std::string message;
    DownloadProgress progress;
};

/**
 * @brief Manages application updates
 * 
 * Features:
 * - Check for updates from GitHub Releases
 * - Download updates in background
 * - Verify download integrity (SHA-256)
 * - Support for update channels (stable/beta/nightly)
 * - Auto-update scheduling
 * - Rollback capability
 */
class UpdateManager {
public:
    using EventCallback = std::function<void(const UpdateEvent&)>;
    using ProgressCallback = std::function<void(const DownloadProgress&)>;
    
    UpdateManager();
    ~UpdateManager();
    
    // Initialization
    bool initialize(const Version& currentVersion, const std::string& updateUrl);
    void shutdown();
    
    // Current version
    Version getCurrentVersion() const;
    void setCurrentVersion(const Version& version);
    
    // Update checking
    bool checkForUpdates();
    bool checkForUpdatesAsync();
    bool isUpdateAvailable() const;
    const UpdateInfo* getAvailableUpdate() const;
    
    // Channel management
    void setUpdateChannel(UpdateChannel channel);
    UpdateChannel getUpdateChannel() const;
    
    // Download
    bool downloadUpdate();
    bool downloadUpdateAsync();
    bool cancelDownload();
    bool isDownloading() const;
    DownloadProgress getDownloadProgress() const;
    std::string getDownloadedFilePath() const;
    
    // Installation
    bool installUpdate();
    bool installUpdateAndRestart();
    bool scheduleInstallOnExit();
    bool isInstallScheduled() const;
    
    // Verification
    bool verifyDownload() const;
    
    // Auto-update settings
    void setAutoCheck(bool enabled);
    bool isAutoCheckEnabled() const;
    void setAutoCheckInterval(int hours);
    int getAutoCheckInterval() const;
    void setAutoDownload(bool enabled);
    bool isAutoDownloadEnabled() const;
    void setAutoInstall(bool enabled);
    bool isAutoInstallEnabled() const;
    
    // Scheduling
    std::chrono::system_clock::time_point getLastCheckTime() const;
    std::chrono::system_clock::time_point getNextCheckTime() const;
    void skipVersion(const Version& version);
    bool isVersionSkipped(const Version& version) const;
    void clearSkippedVersions();
    
    // State
    UpdateState getState() const;
    std::string getStateMessage() const;
    std::string getLastError() const;
    
    // Event handling
    void addEventCallback(EventCallback callback);
    void setProgressCallback(ProgressCallback callback);
    
    // Rollback (if previous version is available)
    bool canRollback() const;
    bool rollback();
    Version getPreviousVersion() const;
    
    // Changelog
    std::string getChangelog() const;
    std::string getChangelogHtml() const;
    
    // Persistence
    bool saveSettings() const;
    bool loadSettings();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

}} // namespace opacity::core

#endif // OPACITY_CORE_UPDATE_MANAGER_H
