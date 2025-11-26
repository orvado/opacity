#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opacity::core
{
    /**
     * @brief Session state for recovery
     */
    struct SessionState
    {
        std::string sessionId;
        std::chrono::system_clock::time_point timestamp;
        
        // Window state
        int windowX = 0;
        int windowY = 0;
        int windowWidth = 1280;
        int windowHeight = 720;
        bool maximized = false;
        
        // Navigation state
        std::vector<std::filesystem::path> openTabs;
        int activeTabIndex = 0;
        std::filesystem::path leftPanelPath;
        std::filesystem::path rightPanelPath;
        
        // Selection state
        std::vector<std::filesystem::path> selectedItems;
        
        // View state
        int viewMode = 0;  // Details, Icons, etc.
        std::string sortColumn;
        bool sortAscending = true;
        
        // Custom data
        std::string customJson;
    };

    /**
     * @brief Pending operation that can be resumed
     */
    struct PendingOperation
    {
        std::string id;
        std::string type;               // "copy", "move", "delete", etc.
        std::chrono::system_clock::time_point startTime;
        std::vector<std::filesystem::path> sourcePaths;
        std::filesystem::path destination;
        int processedCount = 0;
        int totalCount = 0;
        std::string customData;
    };

    /**
     * @brief Crash report information
     */
    struct CrashReport
    {
        std::string reportId;
        std::chrono::system_clock::time_point timestamp;
        std::string exceptionType;
        std::string exceptionMessage;
        std::string stackTrace;
        SessionState sessionState;
        std::string systemInfo;
        std::string logTail;            // Last N lines of log
    };

    /**
     * @brief Recovery configuration
     */
    struct RecoveryConfig
    {
        std::filesystem::path recoveryPath;     // Directory for recovery files
        int autoSaveIntervalSeconds = 60;       // Auto-save interval
        int maxBackupCount = 5;                 // Max backup files to keep
        int crashReportRetentionDays = 30;      // How long to keep crash reports
        bool enableAutoSave = true;
        bool enableCrashReporting = true;
        bool enableSessionRestore = true;
    };

    /**
     * @brief Callback types
     */
    using SessionSaveCallback = std::function<SessionState()>;
    using SessionRestoreCallback = std::function<void(const SessionState&)>;
    using CrashCallback = std::function<void(const CrashReport&)>;
    using OperationResumeCallback = std::function<void(const PendingOperation&)>;

    /**
     * @brief Crash recovery and auto-save manager
     * 
     * Provides:
     * - Automatic session state saving
     * - Crash detection and reporting
     * - Session restoration after crash
     * - Settings backup management
     * - Pending operation recovery
     */
    class CrashRecovery
    {
    public:
        CrashRecovery();
        ~CrashRecovery();

        // Non-copyable, movable
        CrashRecovery(const CrashRecovery&) = delete;
        CrashRecovery& operator=(const CrashRecovery&) = delete;
        CrashRecovery(CrashRecovery&&) noexcept;
        CrashRecovery& operator=(CrashRecovery&&) noexcept;

        /**
         * @brief Initialize with configuration
         */
        bool Initialize(const RecoveryConfig& config);

        /**
         * @brief Shutdown and cleanup
         */
        void Shutdown();

        /**
         * @brief Check if initialized
         */
        bool IsInitialized() const;

        // ============== Session Management ==============

        /**
         * @brief Start a new session
         */
        std::string StartSession();

        /**
         * @brief End the current session normally
         */
        void EndSession();

        /**
         * @brief Get current session ID
         */
        std::string GetSessionId() const;

        /**
         * @brief Save current session state
         */
        bool SaveSessionState(const SessionState& state);

        /**
         * @brief Load last session state
         */
        std::optional<SessionState> LoadSessionState();

        /**
         * @brief Check if previous session crashed
         */
        bool DidPreviousSessionCrash() const;

        /**
         * @brief Get available sessions for recovery
         */
        std::vector<SessionState> GetRecoverableSessions() const;

        // ============== Auto-Save ==============

        /**
         * @brief Start auto-save timer
         */
        void StartAutoSave();

        /**
         * @brief Stop auto-save timer
         */
        void StopAutoSave();

        /**
         * @brief Check if auto-save is running
         */
        bool IsAutoSaveRunning() const;

        /**
         * @brief Trigger immediate auto-save
         */
        void SaveNow();

        /**
         * @brief Set session save callback
         */
        void SetSessionSaveCallback(SessionSaveCallback callback);

        // ============== Settings Backup ==============

        /**
         * @brief Backup settings file
         */
        bool BackupSettings(const std::filesystem::path& settingsPath);

        /**
         * @brief Restore settings from backup
         */
        bool RestoreSettings(const std::filesystem::path& settingsPath, int backupIndex = 0);

        /**
         * @brief Get available setting backups
         */
        std::vector<std::pair<std::filesystem::path, std::chrono::system_clock::time_point>> 
            GetSettingBackups() const;

        /**
         * @brief Clean old backups
         */
        void CleanOldBackups();

        // ============== Crash Handling ==============

        /**
         * @brief Install crash handlers (call early in startup)
         */
        bool InstallCrashHandlers();

        /**
         * @brief Uninstall crash handlers
         */
        void UninstallCrashHandlers();

        /**
         * @brief Create crash report manually
         */
        CrashReport CreateCrashReport(const std::string& exceptionType,
                                      const std::string& exceptionMessage);

        /**
         * @brief Save crash report
         */
        bool SaveCrashReport(const CrashReport& report);

        /**
         * @brief Get all crash reports
         */
        std::vector<CrashReport> GetCrashReports() const;

        /**
         * @brief Delete crash report
         */
        bool DeleteCrashReport(const std::string& reportId);

        /**
         * @brief Clean old crash reports
         */
        void CleanOldCrashReports();

        /**
         * @brief Set crash callback
         */
        void OnCrash(CrashCallback callback);

        // ============== Operation Recovery ==============

        /**
         * @brief Record pending operation
         */
        void RecordPendingOperation(const PendingOperation& operation);

        /**
         * @brief Update pending operation progress
         */
        void UpdateOperationProgress(const std::string& operationId, int processed);

        /**
         * @brief Complete pending operation
         */
        void CompleteOperation(const std::string& operationId);

        /**
         * @brief Get pending operations from previous session
         */
        std::vector<PendingOperation> GetPendingOperations() const;

        /**
         * @brief Clear pending operation
         */
        void ClearPendingOperation(const std::string& operationId);

        /**
         * @brief Set operation resume callback
         */
        void OnOperationResume(OperationResumeCallback callback);

        // ============== System Info ==============

        /**
         * @brief Get system information for crash reports
         */
        static std::string GetSystemInfo();

        /**
         * @brief Get current stack trace
         */
        static std::string GetStackTrace();

        /**
         * @brief Get last N lines from log file
         */
        std::string GetLogTail(int lines = 100) const;

        // ============== Configuration ==============

        /**
         * @brief Get current configuration
         */
        const RecoveryConfig& GetConfig() const;

        /**
         * @brief Update configuration
         */
        void SetConfig(const RecoveryConfig& config);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace opacity::core
