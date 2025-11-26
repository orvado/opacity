// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#include "opacity/core/BookmarkManager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <filesystem>

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

// JSON serialization for Bookmark
void to_json(json& j, const Bookmark& b) {
    j = json{
        {"id", b.id},
        {"name", b.name},
        {"path", b.path},
        {"icon", b.icon},
        {"shortcut", b.shortcut},
        {"description", b.description},
        {"category", b.category},
        {"sortOrder", b.sortOrder},
        {"isFolder", b.isFolder},
        {"createdAt", TimePointToString(b.createdAt)},
        {"accessedAt", TimePointToString(b.accessedAt)},
        {"accessCount", b.accessCount}
    };
}

void from_json(const json& j, Bookmark& b) {
    j.at("id").get_to(b.id);
    j.at("name").get_to(b.name);
    j.at("path").get_to(b.path);
    if (j.contains("icon")) j.at("icon").get_to(b.icon);
    if (j.contains("shortcut")) j.at("shortcut").get_to(b.shortcut);
    if (j.contains("description")) j.at("description").get_to(b.description);
    if (j.contains("category")) j.at("category").get_to(b.category);
    if (j.contains("sortOrder")) j.at("sortOrder").get_to(b.sortOrder);
    if (j.contains("isFolder")) j.at("isFolder").get_to(b.isFolder);
    if (j.contains("createdAt")) b.createdAt = StringToTimePoint(j.at("createdAt").get<std::string>());
    if (j.contains("accessedAt")) b.accessedAt = StringToTimePoint(j.at("accessedAt").get<std::string>());
    if (j.contains("accessCount")) j.at("accessCount").get_to(b.accessCount);
}

// JSON for QuickAccessItem
void to_json(json& j, const QuickAccessItem& q) {
    j = json{
        {"path", q.path},
        {"displayName", q.displayName},
        {"frequency", q.frequency},
        {"lastAccessed", TimePointToString(q.lastAccessed)},
        {"isPinned", q.isPinned}
    };
}

void from_json(const json& j, QuickAccessItem& q) {
    j.at("path").get_to(q.path);
    if (j.contains("displayName")) j.at("displayName").get_to(q.displayName);
    if (j.contains("frequency")) j.at("frequency").get_to(q.frequency);
    if (j.contains("lastAccessed")) q.lastAccessed = StringToTimePoint(j.at("lastAccessed").get<std::string>());
    if (j.contains("isPinned")) j.at("isPinned").get_to(q.isPinned);
}

class BookmarkManager::Impl {
public:
    std::string configPath;
    std::vector<Bookmark> bookmarks;
    std::vector<QuickAccessItem> quickAccess;
    std::unordered_map<std::string, size_t> bookmarkIndex;  // id -> index
    std::unordered_map<std::string, std::string> shortcutMap; // shortcut -> id
    std::vector<EventCallback> callbacks;
    bool autoSave = true;
    bool initialized = false;
    
    void rebuildIndex() {
        bookmarkIndex.clear();
        shortcutMap.clear();
        for (size_t i = 0; i < bookmarks.size(); ++i) {
            bookmarkIndex[bookmarks[i].id] = i;
            if (!bookmarks[i].shortcut.empty()) {
                shortcutMap[bookmarks[i].shortcut] = bookmarks[i].id;
            }
        }
    }
    
    void notifyEvent(BookmarkEventType type, const std::string& id, const std::string& details = "") {
        BookmarkEvent event{type, id, details};
        for (auto& callback : callbacks) {
            try {
                callback(event);
            } catch (...) {
                spdlog::warn("BookmarkManager: exception in event callback");
            }
        }
    }
    
    void triggerAutoSave() {
        if (autoSave && initialized) {
            // Use the outer class save method through the owner pointer
            // For simplicity, save directly here
            saveToFile();
        }
    }
    
    bool saveToFile() {
        try {
            json j;
            j["version"] = 1;
            j["bookmarks"] = bookmarks;
            j["quickAccess"] = quickAccess;
            
            fs::path dir = fs::path(configPath).parent_path();
            if (!fs::exists(dir)) {
                fs::create_directories(dir);
            }
            
            std::ofstream file(configPath);
            if (!file) {
                spdlog::error("BookmarkManager: failed to open {} for writing", configPath);
                return false;
            }
            
            file << j.dump(2);
            spdlog::debug("BookmarkManager: saved {} bookmarks to {}", bookmarks.size(), configPath);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("BookmarkManager: save failed: {}", e.what());
            return false;
        }
    }
    
    bool loadFromFile() {
        try {
            if (!fs::exists(configPath)) {
                spdlog::info("BookmarkManager: no existing bookmarks file");
                return true; // Not an error, just no data yet
            }
            
            std::ifstream file(configPath);
            if (!file) {
                spdlog::error("BookmarkManager: failed to open {}", configPath);
                return false;
            }
            
            json j = json::parse(file);
            
            if (j.contains("bookmarks")) {
                bookmarks = j["bookmarks"].get<std::vector<Bookmark>>();
            }
            if (j.contains("quickAccess")) {
                quickAccess = j["quickAccess"].get<std::vector<QuickAccessItem>>();
            }
            
            rebuildIndex();
            spdlog::info("BookmarkManager: loaded {} bookmarks", bookmarks.size());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("BookmarkManager: load failed: {}", e.what());
            return false;
        }
    }
};

BookmarkManager::BookmarkManager() : pImpl(std::make_unique<Impl>()) {}

BookmarkManager::~BookmarkManager() {
    shutdown();
}

bool BookmarkManager::initialize(const std::string& configPath) {
    if (pImpl->initialized) {
        spdlog::warn("BookmarkManager: already initialized");
        return true;
    }
    
    pImpl->configPath = configPath;
    if (!pImpl->loadFromFile()) {
        spdlog::warn("BookmarkManager: failed to load, starting fresh");
    }
    
    pImpl->initialized = true;
    spdlog::info("BookmarkManager: initialized with {} bookmarks", pImpl->bookmarks.size());
    return true;
}

void BookmarkManager::shutdown() {
    if (pImpl->initialized) {
        save();
        pImpl->initialized = false;
    }
}

std::string BookmarkManager::addBookmark(const std::string& path, 
                                         const std::string& name,
                                         const std::string& category) {
    Bookmark b;
    b.id = GenerateUUID();
    b.path = path;
    b.name = name.empty() ? fs::path(path).filename().string() : name;
    b.category = category;
    b.createdAt = std::chrono::system_clock::now();
    b.accessedAt = b.createdAt;
    b.sortOrder = static_cast<int>(pImpl->bookmarks.size());
    
    pImpl->bookmarks.push_back(b);
    pImpl->bookmarkIndex[b.id] = pImpl->bookmarks.size() - 1;
    
    pImpl->notifyEvent(BookmarkEventType::Added, b.id, b.name);
    pImpl->triggerAutoSave();
    
    spdlog::info("BookmarkManager: added bookmark '{}' for {}", b.name, path);
    return b.id;
}

bool BookmarkManager::removeBookmark(const std::string& id) {
    auto it = pImpl->bookmarkIndex.find(id);
    if (it == pImpl->bookmarkIndex.end()) {
        return false;
    }
    
    size_t index = it->second;
    std::string name = pImpl->bookmarks[index].name;
    std::string shortcut = pImpl->bookmarks[index].shortcut;
    
    pImpl->bookmarks.erase(pImpl->bookmarks.begin() + index);
    pImpl->rebuildIndex();
    
    if (!shortcut.empty()) {
        pImpl->shortcutMap.erase(shortcut);
    }
    
    pImpl->notifyEvent(BookmarkEventType::Removed, id, name);
    pImpl->triggerAutoSave();
    
    spdlog::info("BookmarkManager: removed bookmark '{}'", name);
    return true;
}

bool BookmarkManager::updateBookmark(const std::string& id, const Bookmark& updated) {
    auto it = pImpl->bookmarkIndex.find(id);
    if (it == pImpl->bookmarkIndex.end()) {
        return false;
    }
    
    size_t index = it->second;
    std::string oldShortcut = pImpl->bookmarks[index].shortcut;
    
    pImpl->bookmarks[index] = updated;
    pImpl->bookmarks[index].id = id; // Preserve original ID
    
    // Update shortcut map
    if (!oldShortcut.empty()) {
        pImpl->shortcutMap.erase(oldShortcut);
    }
    if (!updated.shortcut.empty()) {
        pImpl->shortcutMap[updated.shortcut] = id;
    }
    
    pImpl->notifyEvent(BookmarkEventType::Updated, id, updated.name);
    pImpl->triggerAutoSave();
    
    return true;
}

Bookmark* BookmarkManager::getBookmark(const std::string& id) {
    auto it = pImpl->bookmarkIndex.find(id);
    if (it == pImpl->bookmarkIndex.end()) {
        return nullptr;
    }
    return &pImpl->bookmarks[it->second];
}

const Bookmark* BookmarkManager::getBookmark(const std::string& id) const {
    auto it = pImpl->bookmarkIndex.find(id);
    if (it == pImpl->bookmarkIndex.end()) {
        return nullptr;
    }
    return &pImpl->bookmarks[it->second];
}

std::string BookmarkManager::addCategory(const std::string& name, const std::string& parentCategory) {
    Bookmark folder;
    folder.id = GenerateUUID();
    folder.name = name;
    folder.category = parentCategory;
    folder.isFolder = true;
    folder.createdAt = std::chrono::system_clock::now();
    folder.accessedAt = folder.createdAt;
    folder.sortOrder = static_cast<int>(pImpl->bookmarks.size());
    
    pImpl->bookmarks.push_back(folder);
    pImpl->bookmarkIndex[folder.id] = pImpl->bookmarks.size() - 1;
    
    pImpl->notifyEvent(BookmarkEventType::Added, folder.id, folder.name);
    pImpl->triggerAutoSave();
    
    return folder.id;
}

bool BookmarkManager::removeCategory(const std::string& categoryName, bool removeContents) {
    // Find the category folder
    auto catIt = std::find_if(pImpl->bookmarks.begin(), pImpl->bookmarks.end(),
        [&](const Bookmark& b) { return b.isFolder && b.name == categoryName; });
    
    if (catIt == pImpl->bookmarks.end()) {
        return false;
    }
    
    std::string catId = catIt->id;
    
    if (removeContents) {
        // Remove all bookmarks in this category
        pImpl->bookmarks.erase(
            std::remove_if(pImpl->bookmarks.begin(), pImpl->bookmarks.end(),
                [&](const Bookmark& b) { return b.category == categoryName; }),
            pImpl->bookmarks.end());
    } else {
        // Move bookmarks to root
        for (auto& b : pImpl->bookmarks) {
            if (b.category == categoryName) {
                b.category = "";
            }
        }
    }
    
    // Remove the category folder itself
    pImpl->bookmarks.erase(
        std::remove_if(pImpl->bookmarks.begin(), pImpl->bookmarks.end(),
            [&](const Bookmark& b) { return b.id == catId; }),
        pImpl->bookmarks.end());
    
    pImpl->rebuildIndex();
    pImpl->triggerAutoSave();
    
    return true;
}

bool BookmarkManager::renameCategory(const std::string& oldName, const std::string& newName) {
    bool found = false;
    
    for (auto& b : pImpl->bookmarks) {
        if (b.isFolder && b.name == oldName) {
            b.name = newName;
            found = true;
        }
        if (b.category == oldName) {
            b.category = newName;
        }
    }
    
    if (found) {
        pImpl->triggerAutoSave();
    }
    
    return found;
}

std::vector<std::string> BookmarkManager::getCategories() const {
    std::vector<std::string> categories;
    for (const auto& b : pImpl->bookmarks) {
        if (b.isFolder) {
            categories.push_back(b.name);
        }
    }
    return categories;
}

std::vector<const Bookmark*> BookmarkManager::getAllBookmarks() const {
    std::vector<const Bookmark*> result;
    for (const auto& b : pImpl->bookmarks) {
        result.push_back(&b);
    }
    return result;
}

std::vector<const Bookmark*> BookmarkManager::getBookmarksByCategory(const std::string& category) const {
    std::vector<const Bookmark*> result;
    for (const auto& b : pImpl->bookmarks) {
        if (b.category == category && !b.isFolder) {
            result.push_back(&b);
        }
    }
    std::sort(result.begin(), result.end(), 
        [](const Bookmark* a, const Bookmark* b) { return a->sortOrder < b->sortOrder; });
    return result;
}

std::vector<const Bookmark*> BookmarkManager::searchBookmarks(const std::string& query) const {
    std::vector<const Bookmark*> result;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    for (const auto& b : pImpl->bookmarks) {
        std::string lowerName = b.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        std::string lowerPath = b.path;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
        
        if (lowerName.find(lowerQuery) != std::string::npos ||
            lowerPath.find(lowerQuery) != std::string::npos) {
            result.push_back(&b);
        }
    }
    return result;
}

const Bookmark* BookmarkManager::findByPath(const std::string& path) const {
    for (const auto& b : pImpl->bookmarks) {
        if (b.path == path) {
            return &b;
        }
    }
    return nullptr;
}

const Bookmark* BookmarkManager::findByShortcut(const std::string& shortcut) const {
    auto it = pImpl->shortcutMap.find(shortcut);
    if (it == pImpl->shortcutMap.end()) {
        return nullptr;
    }
    return getBookmark(it->second);
}

void BookmarkManager::recordAccess(const std::string& path) {
    auto now = std::chrono::system_clock::now();
    
    // Update bookmark access count if exists
    for (auto& b : pImpl->bookmarks) {
        if (b.path == path) {
            b.accessedAt = now;
            b.accessCount++;
            pImpl->notifyEvent(BookmarkEventType::Accessed, b.id);
            break;
        }
    }
    
    // Update quick access
    auto it = std::find_if(pImpl->quickAccess.begin(), pImpl->quickAccess.end(),
        [&](const QuickAccessItem& q) { return q.path == path; });
    
    if (it != pImpl->quickAccess.end()) {
        it->frequency++;
        it->lastAccessed = now;
    } else {
        QuickAccessItem item;
        item.path = path;
        item.displayName = fs::path(path).filename().string();
        item.frequency = 1;
        item.lastAccessed = now;
        pImpl->quickAccess.push_back(item);
        
        // Limit quick access history
        if (pImpl->quickAccess.size() > 100) {
            // Remove oldest non-pinned item
            auto oldest = std::min_element(pImpl->quickAccess.begin(), pImpl->quickAccess.end(),
                [](const QuickAccessItem& a, const QuickAccessItem& b) {
                    if (a.isPinned != b.isPinned) return !a.isPinned;
                    return a.lastAccessed < b.lastAccessed;
                });
            if (oldest != pImpl->quickAccess.end() && !oldest->isPinned) {
                pImpl->quickAccess.erase(oldest);
            }
        }
    }
    
    pImpl->triggerAutoSave();
}

std::vector<QuickAccessItem> BookmarkManager::getQuickAccessItems(int maxItems) const {
    std::vector<QuickAccessItem> result = pImpl->quickAccess;
    
    // Sort by: pinned first, then by frequency * recency score
    auto now = std::chrono::system_clock::now();
    std::sort(result.begin(), result.end(),
        [&now](const QuickAccessItem& a, const QuickAccessItem& b) {
            if (a.isPinned != b.isPinned) return a.isPinned;
            
            auto ageA = std::chrono::duration_cast<std::chrono::hours>(now - a.lastAccessed).count();
            auto ageB = std::chrono::duration_cast<std::chrono::hours>(now - b.lastAccessed).count();
            
            double scoreA = a.frequency / (1.0 + ageA * 0.1);
            double scoreB = b.frequency / (1.0 + ageB * 0.1);
            
            return scoreA > scoreB;
        });
    
    if (result.size() > static_cast<size_t>(maxItems)) {
        result.resize(maxItems);
    }
    
    return result;
}

std::vector<QuickAccessItem> BookmarkManager::getRecentItems(int maxItems) const {
    std::vector<QuickAccessItem> result = pImpl->quickAccess;
    
    std::sort(result.begin(), result.end(),
        [](const QuickAccessItem& a, const QuickAccessItem& b) {
            return a.lastAccessed > b.lastAccessed;
        });
    
    if (result.size() > static_cast<size_t>(maxItems)) {
        result.resize(maxItems);
    }
    
    return result;
}

std::vector<QuickAccessItem> BookmarkManager::getFrequentItems(int maxItems) const {
    std::vector<QuickAccessItem> result = pImpl->quickAccess;
    
    std::sort(result.begin(), result.end(),
        [](const QuickAccessItem& a, const QuickAccessItem& b) {
            return a.frequency > b.frequency;
        });
    
    if (result.size() > static_cast<size_t>(maxItems)) {
        result.resize(maxItems);
    }
    
    return result;
}

void BookmarkManager::pinToQuickAccess(const std::string& path) {
    auto it = std::find_if(pImpl->quickAccess.begin(), pImpl->quickAccess.end(),
        [&](const QuickAccessItem& q) { return q.path == path; });
    
    if (it != pImpl->quickAccess.end()) {
        it->isPinned = true;
    } else {
        QuickAccessItem item;
        item.path = path;
        item.displayName = fs::path(path).filename().string();
        item.isPinned = true;
        item.lastAccessed = std::chrono::system_clock::now();
        pImpl->quickAccess.push_back(item);
    }
    
    pImpl->triggerAutoSave();
}

void BookmarkManager::unpinFromQuickAccess(const std::string& path) {
    auto it = std::find_if(pImpl->quickAccess.begin(), pImpl->quickAccess.end(),
        [&](const QuickAccessItem& q) { return q.path == path; });
    
    if (it != pImpl->quickAccess.end()) {
        it->isPinned = false;
    }
    
    pImpl->triggerAutoSave();
}

void BookmarkManager::clearRecentHistory() {
    pImpl->quickAccess.erase(
        std::remove_if(pImpl->quickAccess.begin(), pImpl->quickAccess.end(),
            [](const QuickAccessItem& q) { return !q.isPinned; }),
        pImpl->quickAccess.end());
    
    pImpl->triggerAutoSave();
}

bool BookmarkManager::setShortcut(const std::string& bookmarkId, const std::string& shortcut) {
    if (!isShortcutAvailable(shortcut)) {
        return false;
    }
    
    auto* bookmark = getBookmark(bookmarkId);
    if (!bookmark) {
        return false;
    }
    
    // Clear old shortcut
    if (!bookmark->shortcut.empty()) {
        pImpl->shortcutMap.erase(bookmark->shortcut);
    }
    
    bookmark->shortcut = shortcut;
    pImpl->shortcutMap[shortcut] = bookmarkId;
    
    pImpl->triggerAutoSave();
    return true;
}

bool BookmarkManager::clearShortcut(const std::string& bookmarkId) {
    auto* bookmark = getBookmark(bookmarkId);
    if (!bookmark || bookmark->shortcut.empty()) {
        return false;
    }
    
    pImpl->shortcutMap.erase(bookmark->shortcut);
    bookmark->shortcut = "";
    
    pImpl->triggerAutoSave();
    return true;
}

std::vector<std::pair<std::string, std::string>> BookmarkManager::getAllShortcuts() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& [shortcut, id] : pImpl->shortcutMap) {
        result.emplace_back(shortcut, id);
    }
    return result;
}

bool BookmarkManager::isShortcutAvailable(const std::string& shortcut) const {
    return pImpl->shortcutMap.find(shortcut) == pImpl->shortcutMap.end();
}

bool BookmarkManager::moveBookmark(const std::string& id, const std::string& newCategory, int newSortOrder) {
    auto* bookmark = getBookmark(id);
    if (!bookmark) {
        return false;
    }
    
    bookmark->category = newCategory;
    if (newSortOrder >= 0) {
        bookmark->sortOrder = newSortOrder;
    }
    
    pImpl->notifyEvent(BookmarkEventType::Moved, id, newCategory);
    pImpl->triggerAutoSave();
    return true;
}

bool BookmarkManager::reorderBookmarks(const std::string& category, const std::vector<std::string>& orderedIds) {
    for (size_t i = 0; i < orderedIds.size(); ++i) {
        auto* bookmark = getBookmark(orderedIds[i]);
        if (bookmark && bookmark->category == category) {
            bookmark->sortOrder = static_cast<int>(i);
        }
    }
    
    pImpl->triggerAutoSave();
    return true;
}

bool BookmarkManager::exportBookmarks(const std::string& filePath, bool includeUsageData) const {
    try {
        json j;
        j["version"] = 1;
        j["exportedAt"] = TimePointToString(std::chrono::system_clock::now());
        j["bookmarks"] = json::array();
        
        for (const auto& b : pImpl->bookmarks) {
            json bj;
            bj["name"] = b.name;
            bj["path"] = b.path;
            bj["category"] = b.category;
            bj["isFolder"] = b.isFolder;
            bj["shortcut"] = b.shortcut;
            bj["description"] = b.description;
            
            if (includeUsageData) {
                bj["accessCount"] = b.accessCount;
                bj["createdAt"] = TimePointToString(b.createdAt);
                bj["accessedAt"] = TimePointToString(b.accessedAt);
            }
            
            j["bookmarks"].push_back(bj);
        }
        
        std::ofstream file(filePath);
        if (!file) {
            return false;
        }
        
        file << j.dump(2);
        
        pImpl->notifyEvent(BookmarkEventType::Exported, "", filePath);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("BookmarkManager: export failed: {}", e.what());
        return false;
    }
}

bool BookmarkManager::importBookmarks(const std::string& filePath, bool merge) {
    try {
        std::ifstream file(filePath);
        if (!file) {
            return false;
        }
        
        json j = json::parse(file);
        
        if (!merge) {
            pImpl->bookmarks.clear();
        }
        
        for (const auto& bj : j["bookmarks"]) {
            Bookmark b;
            b.id = GenerateUUID();
            b.name = bj.value("name", "");
            b.path = bj.value("path", "");
            b.category = bj.value("category", "");
            b.isFolder = bj.value("isFolder", false);
            b.shortcut = bj.value("shortcut", "");
            b.description = bj.value("description", "");
            b.accessCount = bj.value("accessCount", 0);
            b.createdAt = std::chrono::system_clock::now();
            b.accessedAt = b.createdAt;
            
            if (bj.contains("createdAt")) {
                b.createdAt = StringToTimePoint(bj["createdAt"]);
            }
            if (bj.contains("accessedAt")) {
                b.accessedAt = StringToTimePoint(bj["accessedAt"]);
            }
            
            // Don't import duplicate paths
            bool duplicate = false;
            for (const auto& existing : pImpl->bookmarks) {
                if (existing.path == b.path && !b.isFolder) {
                    duplicate = true;
                    break;
                }
            }
            
            if (!duplicate) {
                pImpl->bookmarks.push_back(b);
            }
        }
        
        pImpl->rebuildIndex();
        pImpl->triggerAutoSave();
        
        pImpl->notifyEvent(BookmarkEventType::Imported, "", filePath);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("BookmarkManager: import failed: {}", e.what());
        return false;
    }
}

std::string BookmarkManager::exportToClipboard() const {
    json j;
    j["type"] = "opacity-bookmarks";
    j["bookmarks"] = json::array();
    
    for (const auto& b : pImpl->bookmarks) {
        j["bookmarks"].push_back({
            {"name", b.name},
            {"path", b.path},
            {"category", b.category}
        });
    }
    
    return j.dump();
}

bool BookmarkManager::importFromClipboard(const std::string& data) {
    try {
        json j = json::parse(data);
        if (j.value("type", "") != "opacity-bookmarks") {
            return false;
        }
        
        for (const auto& bj : j["bookmarks"]) {
            addBookmark(bj.value("path", ""), bj.value("name", ""), bj.value("category", ""));
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

bool BookmarkManager::save() const {
    return pImpl->saveToFile();
}

bool BookmarkManager::load() {
    return pImpl->loadFromFile();
}

void BookmarkManager::setAutoSave(bool enabled) {
    pImpl->autoSave = enabled;
}

void BookmarkManager::addEventCallback(EventCallback callback) {
    pImpl->callbacks.push_back(std::move(callback));
}

bool BookmarkManager::validatePath(const std::string& path) const {
    return fs::exists(path);
}

std::vector<std::string> BookmarkManager::findInvalidBookmarks() const {
    std::vector<std::string> invalid;
    for (const auto& b : pImpl->bookmarks) {
        if (!b.isFolder && !fs::exists(b.path)) {
            invalid.push_back(b.id);
        }
    }
    return invalid;
}

int BookmarkManager::removeInvalidBookmarks() {
    auto invalid = findInvalidBookmarks();
    for (const auto& id : invalid) {
        removeBookmark(id);
    }
    return static_cast<int>(invalid.size());
}

size_t BookmarkManager::getBookmarkCount() const {
    return std::count_if(pImpl->bookmarks.begin(), pImpl->bookmarks.end(),
        [](const Bookmark& b) { return !b.isFolder; });
}

size_t BookmarkManager::getCategoryCount() const {
    return std::count_if(pImpl->bookmarks.begin(), pImpl->bookmarks.end(),
        [](const Bookmark& b) { return b.isFolder; });
}

std::string BookmarkManager::getMostAccessedBookmark() const {
    const Bookmark* mostAccessed = nullptr;
    int maxCount = 0;
    
    for (const auto& b : pImpl->bookmarks) {
        if (!b.isFolder && b.accessCount > maxCount) {
            maxCount = b.accessCount;
            mostAccessed = &b;
        }
    }
    
    return mostAccessed ? mostAccessed->id : "";
}

}} // namespace opacity::core
