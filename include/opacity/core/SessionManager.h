// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#ifndef OPACITY_CORE_SESSION_MANAGER_H
#define OPACITY_CORE_SESSION_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <memory>
#include <map>

namespace opacity { namespace core {

/**
 * @brief Tab state within a session
 */
struct TabState {
    std::string id;
    std::string path;
    std::string displayName;
    std::string viewMode;         // "list", "grid", "details"
    std::string sortColumn;
    bool sortAscending = true;
    std::vector<std::string> selectedItems;
    std::string scrollPosition;
    bool isActive = false;
};

/**
 * @brief Pane state within a session
 */
struct PaneState {
    std::string id;
    std::string type;             // "files", "preview", "search", etc.
    float sizeRatio = 1.0f;       // Relative size in split
    bool isVisible = true;
    std::vector<TabState> tabs;
    int activeTabIndex = 0;
    std::map<std::string, std::string> customState;  // Pane-specific state
};

/**
 * @brief Window state within a session
 */
struct WindowState {
    int x = 100, y = 100;
    int width = 1280, height = 720;
    bool isMaximized = false;
    bool isMinimized = false;
    int monitor = 0;              // Which monitor the window is on
};

/**
 * @brief Complete session state
 */
struct Session {
    std::string id;               // Unique identifier
    std::string name;             // Display name
    std::string description;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point modifiedAt;
    WindowState window;
    std::vector<PaneState> panes;
    std::string layoutType;       // "single", "dual", "triple", "custom"
    std::map<std::string, std::string> globalSettings;  // Session-specific settings
    bool isAutoSave = false;      // True for auto-saved sessions
    
    bool isEmpty() const { return panes.empty(); }
};

/**
 * @brief Workspace profile containing multiple named sessions
 */
struct WorkspaceProfile {
    std::string id;
    std::string name;
    std::string description;
    std::string iconPath;
    std::vector<std::string> sessionIds;
    std::string defaultSessionId;
    std::map<std::string, std::string> settings;
    std::chrono::system_clock::time_point createdAt;
};

/**
 * @brief Session event types
 */
enum class SessionEventType {
    Saved,
    Loaded,
    Deleted,
    AutoSaved,
    Renamed,
    ProfileChanged
};

/**
 * @brief Session event data
 */
struct SessionEvent {
    SessionEventType type;
    std::string sessionId;
    std::string details;
};

/**
 * @brief Manages workspace sessions and state persistence
 * 
 * Provides session management features:
 * - Save and restore complete application state
 * - Named sessions for different workflows
 * - Workspace profiles for project-specific setups
 * - Auto-save and crash recovery
 * - Session templates
 */
class SessionManager {
public:
    using EventCallback = std::function<void(const SessionEvent&)>;
    using StateCollector = std::function<Session()>;
    using StateRestorer = std::function<bool(const Session&)>;
    
    SessionManager();
    ~SessionManager();
    
    // Initialization
    bool initialize(const std::string& configPath);
    void shutdown();
    
    // State capture/restore callbacks
    void setStateCollector(StateCollector collector);
    void setStateRestorer(StateRestorer restorer);
    
    // Current session
    std::string saveCurrentSession(const std::string& name = "", 
                                   const std::string& description = "");
    bool restoreSession(const std::string& sessionId);
    bool hasUnsavedChanges() const;
    std::string getCurrentSessionId() const;
    Session* getCurrentSession();
    
    // Session CRUD
    std::string createSession(const Session& session);
    bool updateSession(const std::string& sessionId, const Session& updated);
    bool deleteSession(const std::string& sessionId);
    Session* getSession(const std::string& sessionId);
    const Session* getSession(const std::string& sessionId) const;
    
    // Session queries
    std::vector<const Session*> getAllSessions() const;
    std::vector<const Session*> getRecentSessions(int maxCount = 10) const;
    std::vector<const Session*> searchSessions(const std::string& query) const;
    const Session* getLastSession() const;
    
    // Session management
    bool renameSession(const std::string& sessionId, const std::string& newName);
    bool duplicateSession(const std::string& sessionId, const std::string& newName);
    bool exportSession(const std::string& sessionId, const std::string& filePath) const;
    std::string importSession(const std::string& filePath);
    
    // Templates
    std::string createTemplate(const std::string& sessionId, const std::string& templateName);
    std::string createSessionFromTemplate(const std::string& templateId, const std::string& name);
    std::vector<const Session*> getTemplates() const;
    bool deleteTemplate(const std::string& templateId);
    
    // Workspace profiles
    std::string createProfile(const std::string& name, const std::string& description = "");
    bool deleteProfile(const std::string& profileId);
    bool switchProfile(const std::string& profileId);
    bool addSessionToProfile(const std::string& profileId, const std::string& sessionId);
    bool removeSessionFromProfile(const std::string& profileId, const std::string& sessionId);
    std::vector<const WorkspaceProfile*> getAllProfiles() const;
    WorkspaceProfile* getCurrentProfile();
    const WorkspaceProfile* getCurrentProfile() const;
    
    // Auto-save
    void setAutoSaveInterval(int seconds);
    void enableAutoSave(bool enabled);
    bool isAutoSaveEnabled() const;
    void triggerAutoSave();
    bool restoreAutoSave();
    bool hasAutoSave() const;
    void clearAutoSave();
    
    // Startup behavior
    void setStartupBehavior(const std::string& behavior); // "last", "default", "empty", "ask"
    std::string getStartupBehavior() const;
    bool setDefaultSession(const std::string& sessionId);
    std::string getDefaultSessionId() const;
    
    // Persistence
    bool save() const;
    bool load();
    
    // Event handling
    void addEventCallback(EventCallback callback);
    
    // Statistics
    size_t getSessionCount() const;
    size_t getProfileCount() const;
    std::string getSessionsDirectory() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

}} // namespace opacity::core

#endif // OPACITY_CORE_SESSION_MANAGER_H
