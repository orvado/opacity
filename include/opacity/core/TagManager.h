// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#ifndef OPACITY_CORE_TAG_MANAGER_H
#define OPACITY_CORE_TAG_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <memory>
#include <cstdint>

namespace opacity { namespace core {

/**
 * @brief Color representation for tags
 */
struct TagColor {
    uint8_t r = 128;
    uint8_t g = 128;
    uint8_t b = 128;
    uint8_t a = 255;
    
    TagColor() = default;
    TagColor(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) 
        : r(r_), g(g_), b(b_), a(a_) {}
    
    static TagColor Red() { return TagColor(231, 76, 60); }
    static TagColor Orange() { return TagColor(230, 126, 34); }
    static TagColor Yellow() { return TagColor(241, 196, 15); }
    static TagColor Green() { return TagColor(46, 204, 113); }
    static TagColor Blue() { return TagColor(52, 152, 219); }
    static TagColor Purple() { return TagColor(155, 89, 182); }
    static TagColor Pink() { return TagColor(255, 105, 180); }
    static TagColor Gray() { return TagColor(149, 165, 166); }
    
    uint32_t toRGBA() const {
        return (static_cast<uint32_t>(r) << 24) | 
               (static_cast<uint32_t>(g) << 16) | 
               (static_cast<uint32_t>(b) << 8) | 
               static_cast<uint32_t>(a);
    }
    
    std::string toHex() const;
    static TagColor fromHex(const std::string& hex);
};

/**
 * @brief A tag definition
 */
struct Tag {
    std::string id;              // Unique identifier
    std::string name;            // Display name
    std::string description;
    TagColor color;
    std::string icon;            // Optional icon name
    std::string shortcut;        // Keyboard shortcut to apply this tag
    int sortOrder = 0;
    bool isSystem = false;       // System tags cannot be deleted
    std::chrono::system_clock::time_point createdAt;
    int usageCount = 0;          // How many files have this tag
};

/**
 * @brief A tag assignment to a file
 */
struct TagAssignment {
    std::string tagId;
    std::string filePath;
    std::chrono::system_clock::time_point assignedAt;
    std::string assignedBy;      // User or "auto"
};

/**
 * @brief Smart tag rule for automatic tagging
 */
struct SmartTagRule {
    std::string id;
    std::string name;
    std::string tagId;           // Tag to apply
    bool enabled = true;
    
    // Conditions
    std::string pathPattern;     // Glob pattern for path
    std::string extensionFilter; // Comma-separated extensions
    int64_t minSize = -1;        // Minimum file size (-1 = no limit)
    int64_t maxSize = -1;        // Maximum file size
    std::string contentContains; // Text content contains
    std::chrono::system_clock::time_point modifiedAfter;
    std::chrono::system_clock::time_point modifiedBefore;
    
    std::string description;
};

/**
 * @brief Tag filter for file searches
 */
struct TagFilter {
    std::vector<std::string> includeTags;  // Files must have ALL these tags
    std::vector<std::string> excludeTags;  // Files must NOT have these tags
    std::vector<std::string> anyOfTags;    // Files must have ANY of these tags
};

/**
 * @brief Tag event types
 */
enum class TagEventType {
    TagCreated,
    TagDeleted,
    TagUpdated,
    TagAssigned,
    TagRemoved,
    RuleTriggered
};

/**
 * @brief Tag event data
 */
struct TagEvent {
    TagEventType type;
    std::string tagId;
    std::string filePath;
    std::string details;
};

/**
 * @brief Manages file tags and labels
 * 
 * Provides tagging functionality:
 * - Create and manage custom tags with colors
 * - Assign tags to files and folders
 * - Filter files by tags
 * - Smart tagging rules for automatic categorization
 * - Persists to local database
 */
class TagManager {
public:
    using EventCallback = std::function<void(const TagEvent&)>;
    
    TagManager();
    ~TagManager();
    
    // Initialization
    bool initialize(const std::string& databasePath);
    void shutdown();
    
    // Tag CRUD
    std::string createTag(const std::string& name, const TagColor& color = TagColor::Gray());
    bool deleteTag(const std::string& tagId);
    bool updateTag(const std::string& tagId, const Tag& updated);
    Tag* getTag(const std::string& tagId);
    const Tag* getTag(const std::string& tagId) const;
    Tag* getTagByName(const std::string& name);
    const Tag* getTagByName(const std::string& name) const;
    
    // Tag queries
    std::vector<const Tag*> getAllTags() const;
    std::vector<const Tag*> getTagsByUsage(int minCount = 0) const;
    std::vector<const Tag*> searchTags(const std::string& query) const;
    
    // Tag assignment
    bool assignTag(const std::string& filePath, const std::string& tagId);
    bool removeTag(const std::string& filePath, const std::string& tagId);
    bool toggleTag(const std::string& filePath, const std::string& tagId);
    bool setTags(const std::string& filePath, const std::vector<std::string>& tagIds);
    bool clearTags(const std::string& filePath);
    bool hasTag(const std::string& filePath, const std::string& tagId) const;
    
    // Batch operations
    bool assignTagToMany(const std::vector<std::string>& filePaths, const std::string& tagId);
    bool removeTagFromMany(const std::vector<std::string>& filePaths, const std::string& tagId);
    bool copyTags(const std::string& sourcePath, const std::string& destPath);
    
    // File queries
    std::vector<std::string> getTagsForFile(const std::string& filePath) const;
    std::vector<Tag> getTagObjectsForFile(const std::string& filePath) const;
    std::vector<std::string> getFilesWithTag(const std::string& tagId) const;
    std::vector<std::string> getFilesMatchingFilter(const TagFilter& filter) const;
    int getFileCountForTag(const std::string& tagId) const;
    
    // Smart tagging rules
    std::string addRule(const SmartTagRule& rule);
    bool deleteRule(const std::string& ruleId);
    bool updateRule(const std::string& ruleId, const SmartTagRule& updated);
    SmartTagRule* getRule(const std::string& ruleId);
    std::vector<const SmartTagRule*> getAllRules() const;
    void applyRules(const std::string& filePath);
    void applyAllRules();  // Apply to all tracked files
    
    // Suggested tags
    std::vector<const Tag*> suggestTags(const std::string& filePath) const;
    std::vector<const Tag*> getFrequentTags(int maxCount = 5) const;
    std::vector<const Tag*> getRecentlyUsedTags(int maxCount = 5) const;
    
    // Import/Export
    bool exportTags(const std::string& filePath) const;
    bool importTags(const std::string& filePath, bool merge = true);
    
    // Maintenance
    bool rebuildIndex();
    int cleanupOrphanedAssignments();  // Remove assignments for deleted files
    int getOrphanedCount() const;
    
    // Persistence
    bool save() const;
    bool load();
    
    // Event handling
    void addEventCallback(EventCallback callback);
    
    // Statistics
    size_t getTagCount() const;
    size_t getAssignmentCount() const;
    size_t getRuleCount() const;
    std::vector<std::pair<std::string, int>> getTagUsageStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

}} // namespace opacity::core

#endif // OPACITY_CORE_TAG_MANAGER_H
