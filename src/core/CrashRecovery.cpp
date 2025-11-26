#include "opacity/core/CrashRecovery.h"
#include "opacity/core/Logger.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace opacity::core
{
    using json = nlohmann::json;

    // Generate unique ID
    static std::string GenerateId()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static const char* hex = "0123456789abcdef";

        std::string id;
        id.reserve(32);
        for (int i = 0; i < 32; ++i) {
            id += hex[dis(gen)];
        }
        return id;
    }

    // Get current timestamp as string
    static std::string GetTimestampString()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
        return ss.str();
    }

    class CrashRecovery::Impl
    {
    public:
        RecoveryConfig config_;
        std::string sessionId_;
        bool previousCrash_ = false;
        
        SessionSaveCallback saveCallback_;
        CrashCallback crashCallback_;
        OperationResumeCallback resumeCallback_;
        
        std::vector<PendingOperation> pendingOperations_;
        
        std::thread autoSaveThread_;
        std::atomic<bool> autoSaveRunning_{false};
        std::atomic<bool> initialized_{false};
        
        mutable std::mutex mutex_;

#ifdef _WIN32
        static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionInfo);
        static CrashRecovery::Impl* instance_;
#endif

        void AutoSaveLoop()
        {
            while (autoSaveRunning_) {
                std::this_thread::sleep_for(
                    std::chrono::seconds(config_.autoSaveIntervalSeconds));
                
                if (autoSaveRunning_ && saveCallback_) {
                    try {
                        SessionState state = saveCallback_();
                        state.sessionId = sessionId_;
                        state.timestamp = std::chrono::system_clock::now();
                        SaveState(state);
                    }
                    catch (const std::exception& e) {
                        Logger::Get()->error("CrashRecovery: Auto-save failed: {}", e.what());
                    }
                }
            }
        }

        void SaveState(const SessionState& state)
        {
            try {
                json j;
                j["sessionId"] = state.sessionId;
                j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    state.timestamp.time_since_epoch()).count();
                
                j["window"]["x"] = state.windowX;
                j["window"]["y"] = state.windowY;
                j["window"]["width"] = state.windowWidth;
                j["window"]["height"] = state.windowHeight;
                j["window"]["maximized"] = state.maximized;
                
                json tabs = json::array();
                for (const auto& tab : state.openTabs) {
                    tabs.push_back(tab.string());
                }
                j["tabs"] = tabs;
                j["activeTab"] = state.activeTabIndex;
                
                j["leftPanel"] = state.leftPanelPath.string();
                j["rightPanel"] = state.rightPanelPath.string();
                
                json selected = json::array();
                for (const auto& item : state.selectedItems) {
                    selected.push_back(item.string());
                }
                j["selected"] = selected;
                
                j["viewMode"] = state.viewMode;
                j["sortColumn"] = state.sortColumn;
                j["sortAscending"] = state.sortAscending;
                
                if (!state.customJson.empty()) {
                    j["custom"] = json::parse(state.customJson);
                }

                auto statePath = config_.recoveryPath / "session_state.json";
                std::ofstream file(statePath);
                file << j.dump(2);

                // Also save lock file to indicate active session
                auto lockPath = config_.recoveryPath / "session.lock";
                std::ofstream lock(lockPath);
                lock << sessionId_;
            }
            catch (const std::exception& e) {
                Logger::Get()->error("CrashRecovery: Failed to save state: {}", e.what());
            }
        }

        SessionState LoadState()
        {
            SessionState state;
            
            try {
                auto statePath = config_.recoveryPath / "session_state.json";
                
                std::error_code ec;
                if (!std::filesystem::exists(statePath, ec)) {
                    return state;
                }

                std::ifstream file(statePath);
                json j = json::parse(file);

                state.sessionId = j.value("sessionId", "");
                
                auto ms = j.value("timestamp", 0LL);
                state.timestamp = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(ms));

                if (j.contains("window")) {
                    state.windowX = j["window"].value("x", 0);
                    state.windowY = j["window"].value("y", 0);
                    state.windowWidth = j["window"].value("width", 1280);
                    state.windowHeight = j["window"].value("height", 720);
                    state.maximized = j["window"].value("maximized", false);
                }

                if (j.contains("tabs")) {
                    for (const auto& tab : j["tabs"]) {
                        state.openTabs.push_back(std::filesystem::path(tab.get<std::string>()));
                    }
                }
                state.activeTabIndex = j.value("activeTab", 0);

                state.leftPanelPath = std::filesystem::path(j.value("leftPanel", ""));
                state.rightPanelPath = std::filesystem::path(j.value("rightPanel", ""));

                if (j.contains("selected")) {
                    for (const auto& item : j["selected"]) {
                        state.selectedItems.push_back(
                            std::filesystem::path(item.get<std::string>()));
                    }
                }

                state.viewMode = j.value("viewMode", 0);
                state.sortColumn = j.value("sortColumn", "");
                state.sortAscending = j.value("sortAscending", true);

                if (j.contains("custom")) {
                    state.customJson = j["custom"].dump();
                }
            }
            catch (const std::exception& e) {
                Logger::Get()->error("CrashRecovery: Failed to load state: {}", e.what());
            }

            return state;
        }

        void SaveOperations()
        {
            try {
                std::lock_guard<std::mutex> lock(mutex_);
                
                json j = json::array();
                for (const auto& op : pendingOperations_) {
                    json opJson;
                    opJson["id"] = op.id;
                    opJson["type"] = op.type;
                    opJson["startTime"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                        op.startTime.time_since_epoch()).count();
                    
                    json sources = json::array();
                    for (const auto& src : op.sourcePaths) {
                        sources.push_back(src.string());
                    }
                    opJson["sources"] = sources;
                    opJson["destination"] = op.destination.string();
                    opJson["processed"] = op.processedCount;
                    opJson["total"] = op.totalCount;
                    opJson["custom"] = op.customData;
                    
                    j.push_back(opJson);
                }

                auto opsPath = config_.recoveryPath / "pending_operations.json";
                std::ofstream file(opsPath);
                file << j.dump(2);
            }
            catch (const std::exception& e) {
                Logger::Get()->error("CrashRecovery: Failed to save operations: {}", e.what());
            }
        }

        std::vector<PendingOperation> LoadOperations()
        {
            std::vector<PendingOperation> ops;
            
            try {
                auto opsPath = config_.recoveryPath / "pending_operations.json";
                
                std::error_code ec;
                if (!std::filesystem::exists(opsPath, ec)) {
                    return ops;
                }

                std::ifstream file(opsPath);
                json j = json::parse(file);

                for (const auto& opJson : j) {
                    PendingOperation op;
                    op.id = opJson.value("id", "");
                    op.type = opJson.value("type", "");
                    
                    auto ms = opJson.value("startTime", 0LL);
                    op.startTime = std::chrono::system_clock::time_point(
                        std::chrono::milliseconds(ms));

                    if (opJson.contains("sources")) {
                        for (const auto& src : opJson["sources"]) {
                            op.sourcePaths.push_back(
                                std::filesystem::path(src.get<std::string>()));
                        }
                    }
                    
                    op.destination = std::filesystem::path(
                        opJson.value("destination", ""));
                    op.processedCount = opJson.value("processed", 0);
                    op.totalCount = opJson.value("total", 0);
                    op.customData = opJson.value("custom", "");

                    ops.push_back(std::move(op));
                }
            }
            catch (const std::exception& e) {
                Logger::Get()->error("CrashRecovery: Failed to load operations: {}", e.what());
            }

            return ops;
        }
    };

#ifdef _WIN32
    CrashRecovery::Impl* CrashRecovery::Impl::instance_ = nullptr;

    LONG WINAPI CrashRecovery::Impl::UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
    {
        if (!instance_) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        try {
            CrashReport report;
            report.reportId = GenerateId();
            report.timestamp = std::chrono::system_clock::now();
            report.exceptionType = "UnhandledException";
            
            std::stringstream msg;
            msg << "Exception code: 0x" << std::hex << exceptionInfo->ExceptionRecord->ExceptionCode;
            report.exceptionMessage = msg.str();
            
            report.stackTrace = GetStackTrace();
            
            if (instance_->saveCallback_) {
                report.sessionState = instance_->saveCallback_();
            }
            
            report.systemInfo = GetSystemInfo();
            
            // Save crash report
            instance_->SaveState(report.sessionState);
            
            json j;
            j["reportId"] = report.reportId;
            j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                report.timestamp.time_since_epoch()).count();
            j["exceptionType"] = report.exceptionType;
            j["exceptionMessage"] = report.exceptionMessage;
            j["stackTrace"] = report.stackTrace;
            j["systemInfo"] = report.systemInfo;
            
            auto reportPath = instance_->config_.recoveryPath / 
                ("crash_" + GetTimestampString() + ".json");
            std::ofstream file(reportPath);
            file << j.dump(2);

            if (instance_->crashCallback_) {
                instance_->crashCallback_(report);
            }
        }
        catch (...) {
            // Can't do much if crash handling itself fails
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif

    // ============== CrashRecovery ==============

    CrashRecovery::CrashRecovery()
        : impl_(std::make_unique<Impl>())
    {}

    CrashRecovery::~CrashRecovery()
    {
        Shutdown();
    }

    CrashRecovery::CrashRecovery(CrashRecovery&&) noexcept = default;
    CrashRecovery& CrashRecovery::operator=(CrashRecovery&&) noexcept = default;

    bool CrashRecovery::Initialize(const RecoveryConfig& config)
    {
        impl_->config_ = config;

        // Create recovery directory
        std::error_code ec;
        if (!std::filesystem::exists(config.recoveryPath, ec)) {
            std::filesystem::create_directories(config.recoveryPath, ec);
            if (ec) {
                Logger::Get()->error("CrashRecovery: Failed to create recovery directory: {}", 
                    ec.message());
                return false;
            }
        }

        // Check for previous crash
        auto lockPath = config.recoveryPath / "session.lock";
        impl_->previousCrash_ = std::filesystem::exists(lockPath, ec);

        impl_->initialized_ = true;
        Logger::Get()->info("CrashRecovery: Initialized, previous crash: {}", 
            impl_->previousCrash_);
        return true;
    }

    void CrashRecovery::Shutdown()
    {
        StopAutoSave();
        UninstallCrashHandlers();
        EndSession();
        impl_->initialized_ = false;
    }

    bool CrashRecovery::IsInitialized() const
    {
        return impl_->initialized_;
    }

    std::string CrashRecovery::StartSession()
    {
        impl_->sessionId_ = GenerateId();
        
        // Create lock file
        auto lockPath = impl_->config_.recoveryPath / "session.lock";
        std::ofstream lock(lockPath);
        lock << impl_->sessionId_;
        
        Logger::Get()->info("CrashRecovery: Started session: {}", impl_->sessionId_);
        return impl_->sessionId_;
    }

    void CrashRecovery::EndSession()
    {
        if (impl_->sessionId_.empty()) return;

        // Remove lock file for clean exit
        std::error_code ec;
        auto lockPath = impl_->config_.recoveryPath / "session.lock";
        std::filesystem::remove(lockPath, ec);

        // Clear pending operations
        auto opsPath = impl_->config_.recoveryPath / "pending_operations.json";
        std::filesystem::remove(opsPath, ec);

        Logger::Get()->info("CrashRecovery: Ended session: {}", impl_->sessionId_);
        impl_->sessionId_.clear();
    }

    std::string CrashRecovery::GetSessionId() const
    {
        return impl_->sessionId_;
    }

    bool CrashRecovery::SaveSessionState(const SessionState& state)
    {
        SessionState stateWithId = state;
        stateWithId.sessionId = impl_->sessionId_;
        stateWithId.timestamp = std::chrono::system_clock::now();
        impl_->SaveState(stateWithId);
        return true;
    }

    std::optional<SessionState> CrashRecovery::LoadSessionState()
    {
        SessionState state = impl_->LoadState();
        if (state.sessionId.empty()) {
            return std::nullopt;
        }
        return state;
    }

    bool CrashRecovery::DidPreviousSessionCrash() const
    {
        return impl_->previousCrash_;
    }

    std::vector<SessionState> CrashRecovery::GetRecoverableSessions() const
    {
        std::vector<SessionState> sessions;
        
        auto state = impl_->LoadState();
        if (!state.sessionId.empty()) {
            sessions.push_back(std::move(state));
        }
        
        return sessions;
    }

    void CrashRecovery::StartAutoSave()
    {
        if (impl_->autoSaveRunning_) return;
        
        impl_->autoSaveRunning_ = true;
        impl_->autoSaveThread_ = std::thread(&Impl::AutoSaveLoop, impl_.get());
        
        Logger::Get()->info("CrashRecovery: Auto-save started");
    }

    void CrashRecovery::StopAutoSave()
    {
        impl_->autoSaveRunning_ = false;
        
        if (impl_->autoSaveThread_.joinable()) {
            impl_->autoSaveThread_.join();
        }
        
        Logger::Get()->info("CrashRecovery: Auto-save stopped");
    }

    bool CrashRecovery::IsAutoSaveRunning() const
    {
        return impl_->autoSaveRunning_;
    }

    void CrashRecovery::SaveNow()
    {
        if (!impl_->saveCallback_) return;
        
        try {
            SessionState state = impl_->saveCallback_();
            SaveSessionState(state);
        }
        catch (const std::exception& e) {
            Logger::Get()->error("CrashRecovery: Manual save failed: {}", e.what());
        }
    }

    void CrashRecovery::SetSessionSaveCallback(SessionSaveCallback callback)
    {
        impl_->saveCallback_ = callback;
    }

    bool CrashRecovery::BackupSettings(const std::filesystem::path& settingsPath)
    {
        try {
            std::error_code ec;
            if (!std::filesystem::exists(settingsPath, ec)) {
                return false;
            }

            auto backupDir = impl_->config_.recoveryPath / "settings_backup";
            std::filesystem::create_directories(backupDir, ec);

            auto backupPath = backupDir / (GetTimestampString() + "_" + settingsPath.filename().string());
            std::filesystem::copy_file(settingsPath, backupPath, 
                std::filesystem::copy_options::overwrite_existing, ec);

            if (ec) {
                Logger::Get()->error("CrashRecovery: Failed to backup settings: {}", ec.message());
                return false;
            }

            // Clean old backups
            CleanOldBackups();

            Logger::Get()->info("CrashRecovery: Backed up settings to {}", backupPath.string());
            return true;
        }
        catch (const std::exception& e) {
            Logger::Get()->error("CrashRecovery: Backup exception: {}", e.what());
            return false;
        }
    }

    bool CrashRecovery::RestoreSettings(const std::filesystem::path& settingsPath, int backupIndex)
    {
        auto backups = GetSettingBackups();
        
        if (backupIndex < 0 || backupIndex >= static_cast<int>(backups.size())) {
            return false;
        }

        try {
            std::error_code ec;
            std::filesystem::copy_file(backups[backupIndex].first, settingsPath,
                std::filesystem::copy_options::overwrite_existing, ec);
            
            if (ec) {
                Logger::Get()->error("CrashRecovery: Failed to restore settings: {}", ec.message());
                return false;
            }

            Logger::Get()->info("CrashRecovery: Restored settings from {}", 
                backups[backupIndex].first.string());
            return true;
        }
        catch (const std::exception& e) {
            Logger::Get()->error("CrashRecovery: Restore exception: {}", e.what());
            return false;
        }
    }

    std::vector<std::pair<std::filesystem::path, std::chrono::system_clock::time_point>>
        CrashRecovery::GetSettingBackups() const
    {
        std::vector<std::pair<std::filesystem::path, std::chrono::system_clock::time_point>> backups;
        
        auto backupDir = impl_->config_.recoveryPath / "settings_backup";
        
        std::error_code ec;
        if (!std::filesystem::exists(backupDir, ec)) {
            return backups;
        }

        for (const auto& entry : std::filesystem::directory_iterator(backupDir, ec)) {
            if (entry.is_regular_file()) {
                auto ftime = entry.last_write_time();
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - std::filesystem::file_time_type::clock::now() + 
                    std::chrono::system_clock::now()
                );
                backups.emplace_back(entry.path(), sctp);
            }
        }

        // Sort by time, newest first
        std::sort(backups.begin(), backups.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        return backups;
    }

    void CrashRecovery::CleanOldBackups()
    {
        auto backups = GetSettingBackups();
        
        // Keep only maxBackupCount
        for (size_t i = impl_->config_.maxBackupCount; i < backups.size(); ++i) {
            std::error_code ec;
            std::filesystem::remove(backups[i].first, ec);
        }
    }

    bool CrashRecovery::InstallCrashHandlers()
    {
#ifdef _WIN32
        Impl::instance_ = impl_.get();
        SetUnhandledExceptionFilter(Impl::UnhandledExceptionHandler);
        Logger::Get()->info("CrashRecovery: Crash handlers installed");
        return true;
#else
        return false;
#endif
    }

    void CrashRecovery::UninstallCrashHandlers()
    {
#ifdef _WIN32
        SetUnhandledExceptionFilter(nullptr);
        Impl::instance_ = nullptr;
        Logger::Get()->info("CrashRecovery: Crash handlers uninstalled");
#endif
    }

    CrashReport CrashRecovery::CreateCrashReport(const std::string& exceptionType,
                                                  const std::string& exceptionMessage)
    {
        CrashReport report;
        report.reportId = GenerateId();
        report.timestamp = std::chrono::system_clock::now();
        report.exceptionType = exceptionType;
        report.exceptionMessage = exceptionMessage;
        report.stackTrace = GetStackTrace();
        report.systemInfo = GetSystemInfo();
        report.logTail = GetLogTail();

        if (impl_->saveCallback_) {
            report.sessionState = impl_->saveCallback_();
        }

        return report;
    }

    bool CrashRecovery::SaveCrashReport(const CrashReport& report)
    {
        try {
            json j;
            j["reportId"] = report.reportId;
            j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                report.timestamp.time_since_epoch()).count();
            j["exceptionType"] = report.exceptionType;
            j["exceptionMessage"] = report.exceptionMessage;
            j["stackTrace"] = report.stackTrace;
            j["systemInfo"] = report.systemInfo;
            j["logTail"] = report.logTail;

            auto crashDir = impl_->config_.recoveryPath / "crashes";
            std::error_code ec;
            std::filesystem::create_directories(crashDir, ec);

            auto reportPath = crashDir / ("crash_" + report.reportId + ".json");
            std::ofstream file(reportPath);
            file << j.dump(2);

            Logger::Get()->info("CrashRecovery: Saved crash report: {}", report.reportId);
            return true;
        }
        catch (const std::exception& e) {
            Logger::Get()->error("CrashRecovery: Failed to save crash report: {}", e.what());
            return false;
        }
    }

    std::vector<CrashReport> CrashRecovery::GetCrashReports() const
    {
        std::vector<CrashReport> reports;
        
        auto crashDir = impl_->config_.recoveryPath / "crashes";
        
        std::error_code ec;
        if (!std::filesystem::exists(crashDir, ec)) {
            return reports;
        }

        for (const auto& entry : std::filesystem::directory_iterator(crashDir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;

            try {
                std::ifstream file(entry.path());
                json j = json::parse(file);

                CrashReport report;
                report.reportId = j.value("reportId", "");
                
                auto ms = j.value("timestamp", 0LL);
                report.timestamp = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(ms));
                
                report.exceptionType = j.value("exceptionType", "");
                report.exceptionMessage = j.value("exceptionMessage", "");
                report.stackTrace = j.value("stackTrace", "");
                report.systemInfo = j.value("systemInfo", "");
                report.logTail = j.value("logTail", "");

                reports.push_back(std::move(report));
            }
            catch (...) {
                // Skip corrupt report files
            }
        }

        // Sort by time, newest first
        std::sort(reports.begin(), reports.end(),
            [](const CrashReport& a, const CrashReport& b) {
                return a.timestamp > b.timestamp;
            });

        return reports;
    }

    bool CrashRecovery::DeleteCrashReport(const std::string& reportId)
    {
        auto crashDir = impl_->config_.recoveryPath / "crashes";
        auto reportPath = crashDir / ("crash_" + reportId + ".json");
        
        std::error_code ec;
        return std::filesystem::remove(reportPath, ec);
    }

    void CrashRecovery::CleanOldCrashReports()
    {
        auto reports = GetCrashReports();
        auto cutoff = std::chrono::system_clock::now() - 
            std::chrono::hours(24 * impl_->config_.crashReportRetentionDays);

        for (const auto& report : reports) {
            if (report.timestamp < cutoff) {
                DeleteCrashReport(report.reportId);
            }
        }
    }

    void CrashRecovery::OnCrash(CrashCallback callback)
    {
        impl_->crashCallback_ = callback;
    }

    void CrashRecovery::RecordPendingOperation(const PendingOperation& operation)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->pendingOperations_.push_back(operation);
        impl_->SaveOperations();
    }

    void CrashRecovery::UpdateOperationProgress(const std::string& operationId, int processed)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        for (auto& op : impl_->pendingOperations_) {
            if (op.id == operationId) {
                op.processedCount = processed;
                break;
            }
        }
        impl_->SaveOperations();
    }

    void CrashRecovery::CompleteOperation(const std::string& operationId)
    {
        ClearPendingOperation(operationId);
    }

    std::vector<PendingOperation> CrashRecovery::GetPendingOperations() const
    {
        return impl_->LoadOperations();
    }

    void CrashRecovery::ClearPendingOperation(const std::string& operationId)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        auto& ops = impl_->pendingOperations_;
        ops.erase(std::remove_if(ops.begin(), ops.end(),
            [&operationId](const PendingOperation& op) { return op.id == operationId; }),
            ops.end());
        
        impl_->SaveOperations();
    }

    void CrashRecovery::OnOperationResume(OperationResumeCallback callback)
    {
        impl_->resumeCallback_ = callback;
    }

    std::string CrashRecovery::GetSystemInfo()
    {
        std::stringstream ss;

#ifdef _WIN32
        OSVERSIONINFOEXW osvi = {};
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        
        // Note: GetVersionExW is deprecated but still works
        #pragma warning(disable:4996)
        GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&osvi));
        #pragma warning(default:4996)
        
        ss << "OS: Windows " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion 
           << " Build " << osvi.dwBuildNumber << "\n";

        MEMORYSTATUSEX memInfo = {};
        memInfo.dwLength = sizeof(memInfo);
        GlobalMemoryStatusEx(&memInfo);
        
        ss << "Memory: " << (memInfo.ullTotalPhys / (1024 * 1024)) << " MB total, "
           << (memInfo.ullAvailPhys / (1024 * 1024)) << " MB available\n";

        SYSTEM_INFO sysInfo = {};
        ::GetSystemInfo(&sysInfo);
        ss << "Processors: " << sysInfo.dwNumberOfProcessors << "\n";
#endif

        return ss.str();
    }

    std::string CrashRecovery::GetStackTrace()
    {
        std::stringstream ss;

#ifdef _WIN32
        void* stack[100];
        USHORT frames = CaptureStackBackTrace(0, 100, stack, nullptr);
        
        HANDLE process = GetCurrentProcess();
        SymInitialize(process, nullptr, TRUE);
        
        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        for (USHORT i = 0; i < frames; i++) {
            DWORD64 address = reinterpret_cast<DWORD64>(stack[i]);
            
            if (SymFromAddr(process, address, nullptr, symbol)) {
                ss << i << ": " << symbol->Name << " [0x" << std::hex << symbol->Address << "]\n";
            } else {
                ss << i << ": [0x" << std::hex << address << "]\n";
            }
        }
        
        SymCleanup(process);
#endif

        return ss.str();
    }

    std::string CrashRecovery::GetLogTail(int lines) const
    {
        // This would need to read from the log file
        // For now, return empty - would need log file path
        return "";
    }

    const RecoveryConfig& CrashRecovery::GetConfig() const
    {
        return impl_->config_;
    }

    void CrashRecovery::SetConfig(const RecoveryConfig& config)
    {
        impl_->config_ = config;
    }

} // namespace opacity::core
