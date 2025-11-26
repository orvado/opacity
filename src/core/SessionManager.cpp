// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#include "opacity/core/SessionManager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace opacity { namespace core {

// Generate UUID
static std::string GenerateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* hex = "0123456789abcdef";
    
    std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    for (char& c : uuid) {
        if (c == 'x') {
            c = hex[dis(gen)];
        } else if (c == 'y') {
            c = hex[(dis(gen) & 0x3) | 0x8];
        }
    }
    return uuid;
}

// Convert timepoint to string
static std::string TimePointToString(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    localtime_s(&tm, &time);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

// Parse timepoint from string
static std::chrono::system_clock::time_point StringToTimePoint(const std::string& str) {
    std::tm tm = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// JSON serialization
void to_json(json& j, const TabState& t) {
    j = json{
        {"id", t.id},
        {"path", t.path},
        {"displayName", t.displayName},
        {"viewMode", t.viewMode},
        {"sortColumn", t.sortColumn},
        {"sortAscending", t.sortAscending},
        {"selectedItems", t.selectedItems},
        {"scrollPosition", t.scrollPosition},
        {"isActive", t.isActive}
    };
}

void from_json(const json& j, TabState& t) {
    j.at("id").get_to(t.id);
    j.at("path").get_to(t.path);
    if (j.contains("displayName")) j.at("displayName").get_to(t.displayName);
    if (j.contains("viewMode")) j.at("viewMode").get_to(t.viewMode);
    if (j.contains("sortColumn")) j.at("sortColumn").get_to(t.sortColumn);
    if (j.contains("sortAscending")) j.at("sortAscending").get_to(t.sortAscending);
    if (j.contains("selectedItems")) j.at("selectedItems").get_to(t.selectedItems);
    if (j.contains("scrollPosition")) j.at("scrollPosition").get_to(t.scrollPosition);
    if (j.contains("isActive")) j.at("isActive").get_to(t.isActive);
}

void to_json(json& j, const PaneState& p) {
    j = json{
        {"id", p.id},
        {"type", p.type},
        {"sizeRatio", p.sizeRatio},
        {"isVisible", p.isVisible},
        {"tabs", p.tabs},
        {"activeTabIndex", p.activeTabIndex},
        {"customState", p.customState}
    };
}

void from_json(const json& j, PaneState& p) {
    j.at("id").get_to(p.id);
    if (j.contains("type")) j.at("type").get_to(p.type);
    if (j.contains("sizeRatio")) j.at("sizeRatio").get_to(p.sizeRatio);
    if (j.contains("isVisible")) j.at("isVisible").get_to(p.isVisible);
    if (j.contains("tabs")) j.at("tabs").get_to(p.tabs);
    if (j.contains("activeTabIndex")) j.at("activeTabIndex").get_to(p.activeTabIndex);
    if (j.contains("customState")) {
        for (auto& [key, value] : j.at("customState").items()) {
            p.customState[key] = value.get<std::string>();
        }
    }
}

void to_json(json& j, const WindowState& w) {
    j = json{
        {"x", w.x},
        {"y", w.y},
        {"width", w.width},
        {"height", w.height},
        {"isMaximized", w.isMaximized},
        {"isMinimized", w.isMinimized},
        {"monitor", w.monitor}
    };
}

void from_json(const json& j, WindowState& w) {
    if (j.contains("x")) j.at("x").get_to(w.x);
    if (j.contains("y")) j.at("y").get_to(w.y);
    if (j.contains("width")) j.at("width").get_to(w.width);
    if (j.contains("height")) j.at("height").get_to(w.height);
    if (j.contains("isMaximized")) j.at("isMaximized").get_to(w.isMaximized);
    if (j.contains("isMinimized")) j.at("isMinimized").get_to(w.isMinimized);
    if (j.contains("monitor")) j.at("monitor").get_to(w.monitor);
}

void to_json(json& j, const Session& s) {
    j = json{
        {"id", s.id},
        {"name", s.name},
        {"description", s.description},
        {"createdAt", TimePointToString(s.createdAt)},
        {"modifiedAt", TimePointToString(s.modifiedAt)},
        {"window", s.window},
        {"panes", s.panes},
        {"layoutType", s.layoutType},
        {"globalSettings", s.globalSettings},
        {"isAutoSave", s.isAutoSave}
    };
}

void from_json(const json& j, Session& s) {
    j.at("id").get_to(s.id);
    if (j.contains("name")) j.at("name").get_to(s.name);
    if (j.contains("description")) j.at("description").get_to(s.description);
    if (j.contains("createdAt")) s.createdAt = StringToTimePoint(j.at("createdAt").get<std::string>());
    if (j.contains("modifiedAt")) s.modifiedAt = StringToTimePoint(j.at("modifiedAt").get<std::string>());
    if (j.contains("window")) j.at("window").get_to(s.window);
    if (j.contains("panes")) j.at("panes").get_to(s.panes);
    if (j.contains("layoutType")) j.at("layoutType").get_to(s.layoutType);
    if (j.contains("globalSettings")) {
        for (auto& [key, value] : j.at("globalSettings").items()) {
            s.globalSettings[key] = value.get<std::string>();
        }
    }
    if (j.contains("isAutoSave")) j.at("isAutoSave").get_to(s.isAutoSave);
}

void to_json(json& j, const WorkspaceProfile& p) {
    j = json{
        {"id", p.id},
        {"name", p.name},
        {"description", p.description},
        {"iconPath", p.iconPath},
        {"sessionIds", p.sessionIds},
        {"defaultSessionId", p.defaultSessionId},
        {"settings", p.settings},
        {"createdAt", TimePointToString(p.createdAt)}
    };
}

void from_json(const json& j, WorkspaceProfile& p) {
    j.at("id").get_to(p.id);
    if (j.contains("name")) j.at("name").get_to(p.name);
    if (j.contains("description")) j.at("description").get_to(p.description);
    if (j.contains("iconPath")) j.at("iconPath").get_to(p.iconPath);
    if (j.contains("sessionIds")) j.at("sessionIds").get_to(p.sessionIds);
    if (j.contains("defaultSessionId")) j.at("defaultSessionId").get_to(p.defaultSessionId);
    if (j.contains("settings")) {
        for (auto& [key, value] : j.at("settings").items()) {
            p.settings[key] = value.get<std::string>();
        }
    }
    if (j.contains("createdAt")) p.createdAt = StringToTimePoint(j.at("createdAt").get<std::string>());
}

class SessionManager::Impl {
public:
    std::string configPath;
    std::string sessionsDir;
    std::vector<Session> sessions;
    std::vector<Session> templates;
    std::vector<WorkspaceProfile> profiles;
    std::unordered_map<std::string, size_t> sessionIndex;
    
    std::string currentSessionId;
    std::string currentProfileId;
    std::string defaultSessionId;
    std::string startupBehavior = "last";
    
    StateCollector stateCollector;
    StateRestorer stateRestorer;
    std::vector<EventCallback> callbacks;
    
    // Auto-save
    std::atomic<bool> autoSaveEnabled{false};
    std::atomic<int> autoSaveInterval{300}; // 5 minutes
    std::atomic<bool> stopAutoSave{false};
    std::thread autoSaveThread;
    std::mutex saveMutex;
    
    bool initialized = false;
    
    void rebuildIndex() {
        sessionIndex.clear();
        for (size_t i = 0; i < sessions.size(); ++i) {
            sessionIndex[sessions[i].id] = i;
        }
    }
    
    void notifyEvent(SessionEventType type, const std::string& id, const std::string& details = "") {
        SessionEvent event{type, id, details};
        for (auto& callback : callbacks) {
            try {
                callback(event);
            } catch (...) {
                spdlog::warn("SessionManager: exception in event callback");
            }
        }
    }
    
    bool saveToFile() {
        std::lock_guard<std::mutex> lock(saveMutex);
        try {
            json j;
            j["version"] = 1;
            j["sessions"] = sessions;
            j["templates"] = templates;
            j["profiles"] = profiles;
            j["currentSessionId"] = currentSessionId;
            j["currentProfileId"] = currentProfileId;
            j["defaultSessionId"] = defaultSessionId;
            j["startupBehavior"] = startupBehavior;
            
            fs::path dir = fs::path(configPath).parent_path();
            if (!fs::exists(dir)) {
                fs::create_directories(dir);
            }
            
            std::ofstream file(configPath);
            if (!file) {
                spdlog::error("SessionManager: failed to open {} for writing", configPath);
                return false;
            }
            
            file << j.dump(2);
            spdlog::debug("SessionManager: saved {} sessions to {}", sessions.size(), configPath);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("SessionManager: save failed: {}", e.what());
            return false;
        }
    }
    
    bool loadFromFile() {
        try {
            if (!fs::exists(configPath)) {
                spdlog::info("SessionManager: no existing sessions file");
                return true;
            }
            
            std::ifstream file(configPath);
            if (!file) {
                spdlog::error("SessionManager: failed to open {}", configPath);
                return false;
            }
            
            json j = json::parse(file);
            
            if (j.contains("sessions")) {
                sessions = j["sessions"].get<std::vector<Session>>();
            }
            if (j.contains("templates")) {
                templates = j["templates"].get<std::vector<Session>>();
            }
            if (j.contains("profiles")) {
                profiles = j["profiles"].get<std::vector<WorkspaceProfile>>();
            }
            if (j.contains("currentSessionId")) {
                currentSessionId = j["currentSessionId"];
            }
            if (j.contains("currentProfileId")) {
                currentProfileId = j["currentProfileId"];
            }
            if (j.contains("defaultSessionId")) {
                defaultSessionId = j["defaultSessionId"];
            }
            if (j.contains("startupBehavior")) {
                startupBehavior = j["startupBehavior"];
            }
            
            rebuildIndex();
            spdlog::info("SessionManager: loaded {} sessions", sessions.size());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("SessionManager: load failed: {}", e.what());
            return false;
        }
    }
    
    void autoSaveLoop() {
        while (!stopAutoSave) {
            std::this_thread::sleep_for(std::chrono::seconds(autoSaveInterval.load()));
            if (stopAutoSave) break;
            
            if (autoSaveEnabled && stateCollector) {
                try {
                    Session current = stateCollector();
                    current.id = "autosave";
                    current.name = "Auto-saved Session";
                    current.isAutoSave = true;
                    current.modifiedAt = std::chrono::system_clock::now();
                    
                    // Save to autosave file
                    json j = current;
                    std::string autoSavePath = sessionsDir + "/autosave.json";
                    std::ofstream file(autoSavePath);
                    if (file) {
                        file << j.dump(2);
                        spdlog::debug("SessionManager: auto-saved session");
                        notifyEvent(SessionEventType::AutoSaved, "autosave");
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("SessionManager: auto-save failed: {}", e.what());
                }
            }
        }
    }
};

SessionManager::SessionManager() : pImpl(std::make_unique<Impl>()) {}

SessionManager::~SessionManager() {
    shutdown();
}

bool SessionManager::initialize(const std::string& configPath) {
    if (pImpl->initialized) {
        spdlog::warn("SessionManager: already initialized");
        return true;
    }
    
    pImpl->configPath = configPath;
    pImpl->sessionsDir = fs::path(configPath).parent_path().string() + "/sessions";
    
    // Create sessions directory
    if (!fs::exists(pImpl->sessionsDir)) {
        fs::create_directories(pImpl->sessionsDir);
    }
    
    if (!pImpl->loadFromFile()) {
        spdlog::warn("SessionManager: failed to load, starting fresh");
    }
    
    pImpl->initialized = true;
    spdlog::info("SessionManager: initialized with {} sessions", pImpl->sessions.size());
    return true;
}

void SessionManager::shutdown() {
    if (pImpl->initialized) {
        // Stop auto-save thread
        pImpl->stopAutoSave = true;
        if (pImpl->autoSaveThread.joinable()) {
            pImpl->autoSaveThread.join();
        }
        
        save();
        pImpl->initialized = false;
    }
}

void SessionManager::setStateCollector(StateCollector collector) {
    pImpl->stateCollector = std::move(collector);
}

void SessionManager::setStateRestorer(StateRestorer restorer) {
    pImpl->stateRestorer = std::move(restorer);
}

std::string SessionManager::saveCurrentSession(const std::string& name, const std::string& description) {
    if (!pImpl->stateCollector) {
        spdlog::error("SessionManager: no state collector set");
        return "";
    }
    
    Session session = pImpl->stateCollector();
    session.id = GenerateUUID();
    session.name = name.empty() ? "Session " + std::to_string(pImpl->sessions.size() + 1) : name;
    session.description = description;
    session.createdAt = std::chrono::system_clock::now();
    session.modifiedAt = session.createdAt;
    
    pImpl->sessions.push_back(session);
    pImpl->sessionIndex[session.id] = pImpl->sessions.size() - 1;
    pImpl->currentSessionId = session.id;
    
    pImpl->notifyEvent(SessionEventType::Saved, session.id, session.name);
    pImpl->saveToFile();
    
    spdlog::info("SessionManager: saved session '{}'", session.name);
    return session.id;
}

bool SessionManager::restoreSession(const std::string& sessionId) {
    if (!pImpl->stateRestorer) {
        spdlog::error("SessionManager: no state restorer set");
        return false;
    }
    
    auto it = pImpl->sessionIndex.find(sessionId);
    if (it == pImpl->sessionIndex.end()) {
        spdlog::error("SessionManager: session {} not found", sessionId);
        return false;
    }
    
    const Session& session = pImpl->sessions[it->second];
    
    if (!pImpl->stateRestorer(session)) {
        spdlog::error("SessionManager: failed to restore session {}", sessionId);
        return false;
    }
    
    pImpl->currentSessionId = sessionId;
    pImpl->notifyEvent(SessionEventType::Loaded, sessionId, session.name);
    
    spdlog::info("SessionManager: restored session '{}'", session.name);
    return true;
}

bool SessionManager::hasUnsavedChanges() const {
    // Would need to compare current state with saved state
    return false;
}

std::string SessionManager::getCurrentSessionId() const {
    return pImpl->currentSessionId;
}

Session* SessionManager::getCurrentSession() {
    if (pImpl->currentSessionId.empty()) {
        return nullptr;
    }
    return getSession(pImpl->currentSessionId);
}

std::string SessionManager::createSession(const Session& session) {
    Session s = session;
    if (s.id.empty()) {
        s.id = GenerateUUID();
    }
    s.createdAt = std::chrono::system_clock::now();
    s.modifiedAt = s.createdAt;
    
    pImpl->sessions.push_back(s);
    pImpl->sessionIndex[s.id] = pImpl->sessions.size() - 1;
    
    pImpl->notifyEvent(SessionEventType::Saved, s.id, s.name);
    pImpl->saveToFile();
    
    return s.id;
}

bool SessionManager::updateSession(const std::string& sessionId, const Session& updated) {
    auto it = pImpl->sessionIndex.find(sessionId);
    if (it == pImpl->sessionIndex.end()) {
        return false;
    }
    
    pImpl->sessions[it->second] = updated;
    pImpl->sessions[it->second].id = sessionId; // Preserve ID
    pImpl->sessions[it->second].modifiedAt = std::chrono::system_clock::now();
    
    pImpl->saveToFile();
    return true;
}

bool SessionManager::deleteSession(const std::string& sessionId) {
    auto it = pImpl->sessionIndex.find(sessionId);
    if (it == pImpl->sessionIndex.end()) {
        return false;
    }
    
    std::string name = pImpl->sessions[it->second].name;
    pImpl->sessions.erase(pImpl->sessions.begin() + it->second);
    pImpl->rebuildIndex();
    
    if (pImpl->currentSessionId == sessionId) {
        pImpl->currentSessionId = "";
    }
    
    pImpl->notifyEvent(SessionEventType::Deleted, sessionId, name);
    pImpl->saveToFile();
    
    return true;
}

Session* SessionManager::getSession(const std::string& sessionId) {
    auto it = pImpl->sessionIndex.find(sessionId);
    if (it == pImpl->sessionIndex.end()) {
        return nullptr;
    }
    return &pImpl->sessions[it->second];
}

const Session* SessionManager::getSession(const std::string& sessionId) const {
    auto it = pImpl->sessionIndex.find(sessionId);
    if (it == pImpl->sessionIndex.end()) {
        return nullptr;
    }
    return &pImpl->sessions[it->second];
}

std::vector<const Session*> SessionManager::getAllSessions() const {
    std::vector<const Session*> result;
    for (const auto& s : pImpl->sessions) {
        if (!s.isAutoSave) {
            result.push_back(&s);
        }
    }
    return result;
}

std::vector<const Session*> SessionManager::getRecentSessions(int maxCount) const {
    std::vector<const Session*> result;
    for (const auto& s : pImpl->sessions) {
        if (!s.isAutoSave) {
            result.push_back(&s);
        }
    }
    
    std::sort(result.begin(), result.end(),
        [](const Session* a, const Session* b) {
            return a->modifiedAt > b->modifiedAt;
        });
    
    if (result.size() > static_cast<size_t>(maxCount)) {
        result.resize(maxCount);
    }
    
    return result;
}

std::vector<const Session*> SessionManager::searchSessions(const std::string& query) const {
    std::vector<const Session*> result;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    for (const auto& s : pImpl->sessions) {
        std::string lowerName = s.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        
        if (lowerName.find(lowerQuery) != std::string::npos) {
            result.push_back(&s);
        }
    }
    
    return result;
}

const Session* SessionManager::getLastSession() const {
    if (pImpl->sessions.empty()) {
        return nullptr;
    }
    
    const Session* latest = nullptr;
    for (const auto& s : pImpl->sessions) {
        if (!s.isAutoSave && (!latest || s.modifiedAt > latest->modifiedAt)) {
            latest = &s;
        }
    }
    
    return latest;
}

bool SessionManager::renameSession(const std::string& sessionId, const std::string& newName) {
    auto* session = getSession(sessionId);
    if (!session) {
        return false;
    }
    
    session->name = newName;
    session->modifiedAt = std::chrono::system_clock::now();
    
    pImpl->notifyEvent(SessionEventType::Renamed, sessionId, newName);
    pImpl->saveToFile();
    
    return true;
}

bool SessionManager::duplicateSession(const std::string& sessionId, const std::string& newName) {
    auto* original = getSession(sessionId);
    if (!original) {
        return false;
    }
    
    Session copy = *original;
    copy.id = GenerateUUID();
    copy.name = newName.empty() ? original->name + " (Copy)" : newName;
    copy.createdAt = std::chrono::system_clock::now();
    copy.modifiedAt = copy.createdAt;
    
    pImpl->sessions.push_back(copy);
    pImpl->sessionIndex[copy.id] = pImpl->sessions.size() - 1;
    
    pImpl->saveToFile();
    return true;
}

bool SessionManager::exportSession(const std::string& sessionId, const std::string& filePath) const {
    auto* session = getSession(sessionId);
    if (!session) {
        return false;
    }
    
    try {
        json j = *session;
        j["exportVersion"] = 1;
        j["exportedAt"] = TimePointToString(std::chrono::system_clock::now());
        
        std::ofstream file(filePath);
        if (!file) {
            return false;
        }
        
        file << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("SessionManager: export failed: {}", e.what());
        return false;
    }
}

std::string SessionManager::importSession(const std::string& filePath) {
    try {
        std::ifstream file(filePath);
        if (!file) {
            return "";
        }
        
        json j = json::parse(file);
        Session session = j.get<Session>();
        session.id = GenerateUUID(); // New ID for imported session
        
        pImpl->sessions.push_back(session);
        pImpl->sessionIndex[session.id] = pImpl->sessions.size() - 1;
        
        pImpl->saveToFile();
        return session.id;
    } catch (const std::exception& e) {
        spdlog::error("SessionManager: import failed: {}", e.what());
        return "";
    }
}

std::string SessionManager::createTemplate(const std::string& sessionId, const std::string& templateName) {
    auto* session = getSession(sessionId);
    if (!session) {
        return "";
    }
    
    Session tmpl = *session;
    tmpl.id = "tmpl_" + GenerateUUID();
    tmpl.name = templateName.empty() ? session->name + " Template" : templateName;
    tmpl.createdAt = std::chrono::system_clock::now();
    
    pImpl->templates.push_back(tmpl);
    pImpl->saveToFile();
    
    return tmpl.id;
}

std::string SessionManager::createSessionFromTemplate(const std::string& templateId, const std::string& name) {
    for (const auto& tmpl : pImpl->templates) {
        if (tmpl.id == templateId) {
            Session session = tmpl;
            session.id = GenerateUUID();
            session.name = name.empty() ? tmpl.name : name;
            session.createdAt = std::chrono::system_clock::now();
            session.modifiedAt = session.createdAt;
            
            pImpl->sessions.push_back(session);
            pImpl->sessionIndex[session.id] = pImpl->sessions.size() - 1;
            
            pImpl->saveToFile();
            return session.id;
        }
    }
    
    return "";
}

std::vector<const Session*> SessionManager::getTemplates() const {
    std::vector<const Session*> result;
    for (const auto& t : pImpl->templates) {
        result.push_back(&t);
    }
    return result;
}

bool SessionManager::deleteTemplate(const std::string& templateId) {
    auto it = std::find_if(pImpl->templates.begin(), pImpl->templates.end(),
        [&](const Session& t) { return t.id == templateId; });
    
    if (it != pImpl->templates.end()) {
        pImpl->templates.erase(it);
        pImpl->saveToFile();
        return true;
    }
    
    return false;
}

std::string SessionManager::createProfile(const std::string& name, const std::string& description) {
    WorkspaceProfile profile;
    profile.id = GenerateUUID();
    profile.name = name;
    profile.description = description;
    profile.createdAt = std::chrono::system_clock::now();
    
    pImpl->profiles.push_back(profile);
    pImpl->saveToFile();
    
    return profile.id;
}

bool SessionManager::deleteProfile(const std::string& profileId) {
    auto it = std::find_if(pImpl->profiles.begin(), pImpl->profiles.end(),
        [&](const WorkspaceProfile& p) { return p.id == profileId; });
    
    if (it != pImpl->profiles.end()) {
        pImpl->profiles.erase(it);
        
        if (pImpl->currentProfileId == profileId) {
            pImpl->currentProfileId = "";
        }
        
        pImpl->saveToFile();
        return true;
    }
    
    return false;
}

bool SessionManager::switchProfile(const std::string& profileId) {
    auto it = std::find_if(pImpl->profiles.begin(), pImpl->profiles.end(),
        [&](const WorkspaceProfile& p) { return p.id == profileId; });
    
    if (it == pImpl->profiles.end()) {
        return false;
    }
    
    pImpl->currentProfileId = profileId;
    
    // Optionally restore default session for this profile
    if (!it->defaultSessionId.empty()) {
        restoreSession(it->defaultSessionId);
    }
    
    pImpl->notifyEvent(SessionEventType::ProfileChanged, profileId, it->name);
    pImpl->saveToFile();
    
    return true;
}

bool SessionManager::addSessionToProfile(const std::string& profileId, const std::string& sessionId) {
    for (auto& profile : pImpl->profiles) {
        if (profile.id == profileId) {
            if (std::find(profile.sessionIds.begin(), profile.sessionIds.end(), sessionId) 
                == profile.sessionIds.end()) {
                profile.sessionIds.push_back(sessionId);
                pImpl->saveToFile();
            }
            return true;
        }
    }
    return false;
}

bool SessionManager::removeSessionFromProfile(const std::string& profileId, const std::string& sessionId) {
    for (auto& profile : pImpl->profiles) {
        if (profile.id == profileId) {
            auto it = std::find(profile.sessionIds.begin(), profile.sessionIds.end(), sessionId);
            if (it != profile.sessionIds.end()) {
                profile.sessionIds.erase(it);
                pImpl->saveToFile();
            }
            return true;
        }
    }
    return false;
}

std::vector<const WorkspaceProfile*> SessionManager::getAllProfiles() const {
    std::vector<const WorkspaceProfile*> result;
    for (const auto& p : pImpl->profiles) {
        result.push_back(&p);
    }
    return result;
}

WorkspaceProfile* SessionManager::getCurrentProfile() {
    for (auto& p : pImpl->profiles) {
        if (p.id == pImpl->currentProfileId) {
            return &p;
        }
    }
    return nullptr;
}

const WorkspaceProfile* SessionManager::getCurrentProfile() const {
    for (const auto& p : pImpl->profiles) {
        if (p.id == pImpl->currentProfileId) {
            return &p;
        }
    }
    return nullptr;
}

void SessionManager::setAutoSaveInterval(int seconds) {
    pImpl->autoSaveInterval = seconds;
}

void SessionManager::enableAutoSave(bool enabled) {
    bool wasEnabled = pImpl->autoSaveEnabled.exchange(enabled);
    
    if (enabled && !wasEnabled) {
        // Start auto-save thread
        pImpl->stopAutoSave = false;
        pImpl->autoSaveThread = std::thread(&Impl::autoSaveLoop, pImpl.get());
    } else if (!enabled && wasEnabled) {
        // Stop auto-save thread
        pImpl->stopAutoSave = true;
        if (pImpl->autoSaveThread.joinable()) {
            pImpl->autoSaveThread.join();
        }
    }
}

bool SessionManager::isAutoSaveEnabled() const {
    return pImpl->autoSaveEnabled;
}

void SessionManager::triggerAutoSave() {
    if (pImpl->stateCollector) {
        try {
            Session current = pImpl->stateCollector();
            current.id = "autosave";
            current.name = "Auto-saved Session";
            current.isAutoSave = true;
            current.modifiedAt = std::chrono::system_clock::now();
            
            json j = current;
            std::string autoSavePath = pImpl->sessionsDir + "/autosave.json";
            std::ofstream file(autoSavePath);
            if (file) {
                file << j.dump(2);
                pImpl->notifyEvent(SessionEventType::AutoSaved, "autosave");
            }
        } catch (...) {
            spdlog::warn("SessionManager: manual auto-save failed");
        }
    }
}

bool SessionManager::restoreAutoSave() {
    if (!pImpl->stateRestorer) {
        return false;
    }
    
    std::string autoSavePath = pImpl->sessionsDir + "/autosave.json";
    if (!fs::exists(autoSavePath)) {
        return false;
    }
    
    try {
        std::ifstream file(autoSavePath);
        json j = json::parse(file);
        Session session = j.get<Session>();
        
        return pImpl->stateRestorer(session);
    } catch (const std::exception& e) {
        spdlog::error("SessionManager: failed to restore auto-save: {}", e.what());
        return false;
    }
}

bool SessionManager::hasAutoSave() const {
    std::string autoSavePath = pImpl->sessionsDir + "/autosave.json";
    return fs::exists(autoSavePath);
}

void SessionManager::clearAutoSave() {
    std::string autoSavePath = pImpl->sessionsDir + "/autosave.json";
    if (fs::exists(autoSavePath)) {
        fs::remove(autoSavePath);
    }
}

void SessionManager::setStartupBehavior(const std::string& behavior) {
    pImpl->startupBehavior = behavior;
    pImpl->saveToFile();
}

std::string SessionManager::getStartupBehavior() const {
    return pImpl->startupBehavior;
}

bool SessionManager::setDefaultSession(const std::string& sessionId) {
    if (!getSession(sessionId)) {
        return false;
    }
    pImpl->defaultSessionId = sessionId;
    pImpl->saveToFile();
    return true;
}

std::string SessionManager::getDefaultSessionId() const {
    return pImpl->defaultSessionId;
}

bool SessionManager::save() const {
    return pImpl->saveToFile();
}

bool SessionManager::load() {
    return pImpl->loadFromFile();
}

void SessionManager::addEventCallback(EventCallback callback) {
    pImpl->callbacks.push_back(std::move(callback));
}

size_t SessionManager::getSessionCount() const {
    return std::count_if(pImpl->sessions.begin(), pImpl->sessions.end(),
        [](const Session& s) { return !s.isAutoSave; });
}

size_t SessionManager::getProfileCount() const {
    return pImpl->profiles.size();
}

std::string SessionManager::getSessionsDirectory() const {
    return pImpl->sessionsDir;
}

}} // namespace opacity::core
