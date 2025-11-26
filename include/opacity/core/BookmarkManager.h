// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#ifndef OPACITY_CORE_BOOKMARK_MANAGER_H
#define OPACITY_CORE_BOOKMARK_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <memory>
#include <unordered_map>

namespace opacity { namespace core {

/**
 * @brief A bookmarked location for quick access
 */
struct Bookmark {
    std::string id;              // Unique identifier (UUID)
    std::string name;            // Display name
    std::string path;            // Filesystem path
    std::string icon;            // Custom icon (optional)
    std::string shortcut;        // Keyboard shortcut (e.g., "Ctrl+1")
    std::string description;     // Optional description
    std::string category;        // Category/folder in bookmark tree
    int sortOrder = 0;           // Sort order within category
    bool isFolder = false;       // True if this is a folder/category
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point accessedAt;
    int accessCount = 0;         // Number of times accessed
    
    // Validation
    bool isValid() const { return !path.empty() || isFolder; }
};

/**
 * @brief Quick access item with usage frequency
 */
struct QuickAccessItem {
    std::string path;
    std::string displayName;
    int frequency = 0;
    std::chrono::system_clock::time_point lastAccessed;
    bool isPinned = false;
};

/**
 * @brief Event types for bookmark changes
 */
enum class BookmarkEventType {
    Added,
    Removed,
    Updated,
    Moved,
    Accessed,
    Imported,
    Exported
};

/**
 * @brief Bookmark change event data
 */
struct BookmarkEvent {
    BookmarkEventType type;
    std::string bookmarkId;
    std::string details;
};

/**
 * @brief Manages bookmarks and quick access locations
 * 
 * Provides persistent storage for favorite locations with:
 * - Hierarchical organization via categories
 * - Keyboard shortcuts for quick navigation
 * - Usage tracking for intelligent suggestions
 * - Import/export for backup and sharing
 * - Quick access sidebar support
 */
class BookmarkManager {
public:
    using EventCallback = std::function<void(const BookmarkEvent&)>;
    
    BookmarkManager();
    ~BookmarkManager();
    
    // Initialization
    bool initialize(const std::string& configPath);
    void shutdown();
    
    // Bookmark CRUD operations
    std::string addBookmark(const std::string& path, 
                           const std::string& name = "",
                           const std::string& category = "");
    bool removeBookmark(const std::string& id);
    bool updateBookmark(const std::string& id, const Bookmark& updated);
    Bookmark* getBookmark(const std::string& id);
    const Bookmark* getBookmark(const std::string& id) const;
    
    // Category management
    std::string addCategory(const std::string& name, const std::string& parentCategory = "");
    bool removeCategory(const std::string& categoryName, bool removeContents = false);
    bool renameCategory(const std::string& oldName, const std::string& newName);
    std::vector<std::string> getCategories() const;
    
    // Queries
    std::vector<const Bookmark*> getAllBookmarks() const;
    std::vector<const Bookmark*> getBookmarksByCategory(const std::string& category) const;
    std::vector<const Bookmark*> searchBookmarks(const std::string& query) const;
    const Bookmark* findByPath(const std::string& path) const;
    const Bookmark* findByShortcut(const std::string& shortcut) const;
    
    // Quick Access
    void recordAccess(const std::string& path);
    std::vector<QuickAccessItem> getQuickAccessItems(int maxItems = 10) const;
    std::vector<QuickAccessItem> getRecentItems(int maxItems = 10) const;
    std::vector<QuickAccessItem> getFrequentItems(int maxItems = 10) const;
    void pinToQuickAccess(const std::string& path);
    void unpinFromQuickAccess(const std::string& path);
    void clearRecentHistory();
    
    // Keyboard shortcuts
    bool setShortcut(const std::string& bookmarkId, const std::string& shortcut);
    bool clearShortcut(const std::string& bookmarkId);
    std::vector<std::pair<std::string, std::string>> getAllShortcuts() const;
    bool isShortcutAvailable(const std::string& shortcut) const;
    
    // Reordering
    bool moveBookmark(const std::string& id, const std::string& newCategory, int newSortOrder = -1);
    bool reorderBookmarks(const std::string& category, const std::vector<std::string>& orderedIds);
    
    // Import/Export
    bool exportBookmarks(const std::string& filePath, bool includeUsageData = true) const;
    bool importBookmarks(const std::string& filePath, bool merge = true);
    std::string exportToClipboard() const;
    bool importFromClipboard(const std::string& data);
    
    // Persistence
    bool save() const;
    bool load();
    void setAutoSave(bool enabled);
    
    // Event handling
    void addEventCallback(EventCallback callback);
    
    // Validation
    bool validatePath(const std::string& path) const;
    std::vector<std::string> findInvalidBookmarks() const;
    int removeInvalidBookmarks();
    
    // Statistics
    size_t getBookmarkCount() const;
    size_t getCategoryCount() const;
    std::string getMostAccessedBookmark() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

}} // namespace opacity::core

#endif // OPACITY_CORE_BOOKMARK_MANAGER_H
