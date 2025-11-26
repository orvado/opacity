// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#include "opacity/core/TagManager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <unordered_map>
#include <set>
#include <regex>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace opacity { namespace core {

// TagColor implementation
std::string TagColor::toHex() const {
    std::ostringstream oss;
    oss << "#" << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<int>(r)
        << std::setw(2) << static_cast<int>(g)
        << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

TagColor TagColor::fromHex(const std::string& hex) {
    TagColor color;
    std::string h = hex;
    if (!h.empty() && h[0] == '#') {
        h = h.substr(1);
    }
    
    if (h.length() >= 6) {
        color.r = static_cast<uint8_t>(std::stoi(h.substr(0, 2), nullptr, 16));
        color.g = static_cast<uint8_t>(std::stoi(h.substr(2, 2), nullptr, 16));
        color.b = static_cast<uint8_t>(std::stoi(h.substr(4, 2), nullptr, 16));
    }
    if (h.length() >= 8) {
        color.a = static_cast<uint8_t>(std::stoi(h.substr(6, 2), nullptr, 16));
    }
    
    return color;
}

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
void to_json(json& j, const TagColor& c) {
    j = c.toHex();
}

void from_json(const json& j, TagColor& c) {
    c = TagColor::fromHex(j.get<std::string>());
}

void to_json(json& j, const Tag& t) {
    j = json{
        {"id", t.id},
        {"name", t.name},
        {"description", t.description},
        {"color", t.color},
        {"icon", t.icon},
        {"shortcut", t.shortcut},
        {"sortOrder", t.sortOrder},
        {"isSystem", t.isSystem},
        {"createdAt", TimePointToString(t.createdAt)},
        {"usageCount", t.usageCount}
    };
}

void from_json(const json& j, Tag& t) {
    j.at("id").get_to(t.id);
    j.at("name").get_to(t.name);
    if (j.contains("description")) j.at("description").get_to(t.description);
    if (j.contains("color")) j.at("color").get_to(t.color);
    if (j.contains("icon")) j.at("icon").get_to(t.icon);
    if (j.contains("shortcut")) j.at("shortcut").get_to(t.shortcut);
    if (j.contains("sortOrder")) j.at("sortOrder").get_to(t.sortOrder);
    if (j.contains("isSystem")) j.at("isSystem").get_to(t.isSystem);
    if (j.contains("createdAt")) t.createdAt = StringToTimePoint(j.at("createdAt").get<std::string>());
    if (j.contains("usageCount")) j.at("usageCount").get_to(t.usageCount);
}

void to_json(json& j, const TagAssignment& a) {
    j = json{
        {"tagId", a.tagId},
        {"filePath", a.filePath},
        {"assignedAt", TimePointToString(a.assignedAt)},
        {"assignedBy", a.assignedBy}
    };
}

void from_json(const json& j, TagAssignment& a) {
    j.at("tagId").get_to(a.tagId);
    j.at("filePath").get_to(a.filePath);
    if (j.contains("assignedAt")) a.assignedAt = StringToTimePoint(j.at("assignedAt").get<std::string>());
    if (j.contains("assignedBy")) j.at("assignedBy").get_to(a.assignedBy);
}

void to_json(json& j, const SmartTagRule& r) {
    j = json{
        {"id", r.id},
        {"name", r.name},
        {"tagId", r.tagId},
        {"enabled", r.enabled},
        {"pathPattern", r.pathPattern},
        {"extensionFilter", r.extensionFilter},
        {"minSize", r.minSize},
        {"maxSize", r.maxSize},
        {"contentContains", r.contentContains},
        {"description", r.description}
    };
}

void from_json(const json& j, SmartTagRule& r) {
    j.at("id").get_to(r.id);
    if (j.contains("name")) j.at("name").get_to(r.name);
    if (j.contains("tagId")) j.at("tagId").get_to(r.tagId);
    if (j.contains("enabled")) j.at("enabled").get_to(r.enabled);
    if (j.contains("pathPattern")) j.at("pathPattern").get_to(r.pathPattern);
    if (j.contains("extensionFilter")) j.at("extensionFilter").get_to(r.extensionFilter);
    if (j.contains("minSize")) j.at("minSize").get_to(r.minSize);
    if (j.contains("maxSize")) j.at("maxSize").get_to(r.maxSize);
    if (j.contains("contentContains")) j.at("contentContains").get_to(r.contentContains);
    if (j.contains("description")) j.at("description").get_to(r.description);
}

class TagManager::Impl {
public:
    std::string databasePath;
    std::vector<Tag> tags;
    std::vector<TagAssignment> assignments;
    std::vector<SmartTagRule> rules;
    
    std::unordered_map<std::string, size_t> tagIndex;       // id -> index
    std::unordered_map<std::string, std::string> nameIndex; // name -> id
    std::unordered_map<std::string, std::set<std::string>> fileToTags;  // path -> tag ids
    std::unordered_map<std::string, std::set<std::string>> tagToFiles;  // tag id -> paths
    
    std::vector<EventCallback> callbacks;
    bool initialized = false;
    
    void rebuildIndex() {
        tagIndex.clear();
        nameIndex.clear();
        fileToTags.clear();
        tagToFiles.clear();
        
        for (size_t i = 0; i < tags.size(); ++i) {
            tagIndex[tags[i].id] = i;
            nameIndex[tags[i].name] = tags[i].id;
        }
        
        for (const auto& a : assignments) {
            fileToTags[a.filePath].insert(a.tagId);
            tagToFiles[a.tagId].insert(a.filePath);
        }
        
        // Update usage counts
        for (auto& tag : tags) {
            auto it = tagToFiles.find(tag.id);
            tag.usageCount = it != tagToFiles.end() ? static_cast<int>(it->second.size()) : 0;
        }
    }
    
    void notifyEvent(TagEventType type, const std::string& tagId, 
                    const std::string& filePath = "", const std::string& details = "") {
        TagEvent event{type, tagId, filePath, details};
        for (auto& callback : callbacks) {
            try {
                callback(event);
            } catch (...) {
                spdlog::warn("TagManager: exception in event callback");
            }
        }
    }
    
    bool saveToFile() {
        try {
            json j;
            j["version"] = 1;
            j["tags"] = tags;
            j["assignments"] = assignments;
            j["rules"] = rules;
            
            fs::path dir = fs::path(databasePath).parent_path();
            if (!fs::exists(dir)) {
                fs::create_directories(dir);
            }
            
            std::ofstream file(databasePath);
            if (!file) {
                spdlog::error("TagManager: failed to open {} for writing", databasePath);
                return false;
            }
            
            file << j.dump(2);
            spdlog::debug("TagManager: saved {} tags, {} assignments", 
                         tags.size(), assignments.size());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("TagManager: save failed: {}", e.what());
            return false;
        }
    }
    
    bool loadFromFile() {
        try {
            if (!fs::exists(databasePath)) {
                spdlog::info("TagManager: no existing database");
                createDefaultTags();
                return true;
            }
            
            std::ifstream file(databasePath);
            if (!file) {
                spdlog::error("TagManager: failed to open {}", databasePath);
                return false;
            }
            
            json j = json::parse(file);
            
            if (j.contains("tags")) {
                tags = j["tags"].get<std::vector<Tag>>();
            }
            if (j.contains("assignments")) {
                assignments = j["assignments"].get<std::vector<TagAssignment>>();
            }
            if (j.contains("rules")) {
                rules = j["rules"].get<std::vector<SmartTagRule>>();
            }
            
            rebuildIndex();
            spdlog::info("TagManager: loaded {} tags, {} assignments", 
                        tags.size(), assignments.size());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("TagManager: load failed: {}", e.what());
            return false;
        }
    }
    
    void createDefaultTags() {
        // Create some default tags
        auto addTag = [this](const std::string& name, const TagColor& color, bool isSystem = true) {
            Tag t;
            t.id = GenerateUUID();
            t.name = name;
            t.color = color;
            t.isSystem = isSystem;
            t.createdAt = std::chrono::system_clock::now();
            tags.push_back(t);
        };
        
        addTag("Important", TagColor::Red());
        addTag("Work", TagColor::Blue());
        addTag("Personal", TagColor::Green());
        addTag("Archive", TagColor::Gray());
        addTag("Review", TagColor::Orange());
        
        rebuildIndex();
    }
    
    bool matchesRule(const SmartTagRule& rule, const std::string& filePath) {
        if (!rule.enabled) return false;
        
        try {
            // Check path pattern
            if (!rule.pathPattern.empty()) {
                std::regex pathRegex(rule.pathPattern, std::regex::icase);
                if (!std::regex_search(filePath, pathRegex)) {
                    return false;
                }
            }
            
            // Check extension filter
            if (!rule.extensionFilter.empty()) {
                std::string ext = fs::path(filePath).extension().string();
                if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                
                std::istringstream iss(rule.extensionFilter);
                std::string token;
                bool found = false;
                while (std::getline(iss, token, ',')) {
                    // Trim whitespace
                    token.erase(0, token.find_first_not_of(" \t"));
                    token.erase(token.find_last_not_of(" \t") + 1);
                    if (token[0] == '.') token = token.substr(1);
                    
                    if (_stricmp(ext.c_str(), token.c_str()) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            
            // Check file size
            if (fs::exists(filePath) && fs::is_regular_file(filePath)) {
                auto size = static_cast<int64_t>(fs::file_size(filePath));
                if (rule.minSize >= 0 && size < rule.minSize) return false;
                if (rule.maxSize >= 0 && size > rule.maxSize) return false;
            }
            
            return true;
        } catch (...) {
            return false;
        }
    }
};

TagManager::TagManager() : pImpl(std::make_unique<Impl>()) {}

TagManager::~TagManager() {
    shutdown();
}

bool TagManager::initialize(const std::string& databasePath) {
    if (pImpl->initialized) {
        spdlog::warn("TagManager: already initialized");
        return true;
    }
    
    pImpl->databasePath = databasePath;
    if (!pImpl->loadFromFile()) {
        spdlog::warn("TagManager: failed to load, starting fresh");
        pImpl->createDefaultTags();
    }
    
    pImpl->initialized = true;
    spdlog::info("TagManager: initialized with {} tags", pImpl->tags.size());
    return true;
}

void TagManager::shutdown() {
    if (pImpl->initialized) {
        save();
        pImpl->initialized = false;
    }
}

std::string TagManager::createTag(const std::string& name, const TagColor& color) {
    // Check for duplicate name
    if (pImpl->nameIndex.find(name) != pImpl->nameIndex.end()) {
        spdlog::warn("TagManager: tag '{}' already exists", name);
        return "";
    }
    
    Tag t;
    t.id = GenerateUUID();
    t.name = name;
    t.color = color;
    t.createdAt = std::chrono::system_clock::now();
    t.sortOrder = static_cast<int>(pImpl->tags.size());
    
    pImpl->tags.push_back(t);
    pImpl->tagIndex[t.id] = pImpl->tags.size() - 1;
    pImpl->nameIndex[t.name] = t.id;
    
    pImpl->notifyEvent(TagEventType::TagCreated, t.id);
    save();
    
    spdlog::info("TagManager: created tag '{}'", name);
    return t.id;
}

bool TagManager::deleteTag(const std::string& tagId) {
    auto it = pImpl->tagIndex.find(tagId);
    if (it == pImpl->tagIndex.end()) {
        return false;
    }
    
    Tag& tag = pImpl->tags[it->second];
    if (tag.isSystem) {
        spdlog::warn("TagManager: cannot delete system tag '{}'", tag.name);
        return false;
    }
    
    std::string name = tag.name;
    
    // Remove all assignments for this tag
    pImpl->assignments.erase(
        std::remove_if(pImpl->assignments.begin(), pImpl->assignments.end(),
            [&](const TagAssignment& a) { return a.tagId == tagId; }),
        pImpl->assignments.end());
    
    // Remove the tag
    pImpl->tags.erase(pImpl->tags.begin() + it->second);
    pImpl->rebuildIndex();
    
    pImpl->notifyEvent(TagEventType::TagDeleted, tagId, "", name);
    save();
    
    spdlog::info("TagManager: deleted tag '{}'", name);
    return true;
}

bool TagManager::updateTag(const std::string& tagId, const Tag& updated) {
    auto it = pImpl->tagIndex.find(tagId);
    if (it == pImpl->tagIndex.end()) {
        return false;
    }
    
    std::string oldName = pImpl->tags[it->second].name;
    pImpl->tags[it->second] = updated;
    pImpl->tags[it->second].id = tagId; // Preserve ID
    
    // Update name index if name changed
    if (oldName != updated.name) {
        pImpl->nameIndex.erase(oldName);
        pImpl->nameIndex[updated.name] = tagId;
    }
    
    pImpl->notifyEvent(TagEventType::TagUpdated, tagId);
    save();
    
    return true;
}

Tag* TagManager::getTag(const std::string& tagId) {
    auto it = pImpl->tagIndex.find(tagId);
    if (it == pImpl->tagIndex.end()) {
        return nullptr;
    }
    return &pImpl->tags[it->second];
}

const Tag* TagManager::getTag(const std::string& tagId) const {
    auto it = pImpl->tagIndex.find(tagId);
    if (it == pImpl->tagIndex.end()) {
        return nullptr;
    }
    return &pImpl->tags[it->second];
}

Tag* TagManager::getTagByName(const std::string& name) {
    auto it = pImpl->nameIndex.find(name);
    if (it == pImpl->nameIndex.end()) {
        return nullptr;
    }
    return getTag(it->second);
}

const Tag* TagManager::getTagByName(const std::string& name) const {
    auto it = pImpl->nameIndex.find(name);
    if (it == pImpl->nameIndex.end()) {
        return nullptr;
    }
    return getTag(it->second);
}

std::vector<const Tag*> TagManager::getAllTags() const {
    std::vector<const Tag*> result;
    for (const auto& t : pImpl->tags) {
        result.push_back(&t);
    }
    std::sort(result.begin(), result.end(),
        [](const Tag* a, const Tag* b) { return a->sortOrder < b->sortOrder; });
    return result;
}

std::vector<const Tag*> TagManager::getTagsByUsage(int minCount) const {
    std::vector<const Tag*> result;
    for (const auto& t : pImpl->tags) {
        if (t.usageCount >= minCount) {
            result.push_back(&t);
        }
    }
    std::sort(result.begin(), result.end(),
        [](const Tag* a, const Tag* b) { return a->usageCount > b->usageCount; });
    return result;
}

std::vector<const Tag*> TagManager::searchTags(const std::string& query) const {
    std::vector<const Tag*> result;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    for (const auto& t : pImpl->tags) {
        std::string lowerName = t.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        
        if (lowerName.find(lowerQuery) != std::string::npos) {
            result.push_back(&t);
        }
    }
    return result;
}

bool TagManager::assignTag(const std::string& filePath, const std::string& tagId) {
    if (!getTag(tagId)) {
        spdlog::warn("TagManager: unknown tag {}", tagId);
        return false;
    }
    
    // Check if already assigned
    if (hasTag(filePath, tagId)) {
        return true;
    }
    
    TagAssignment a;
    a.tagId = tagId;
    a.filePath = filePath;
    a.assignedAt = std::chrono::system_clock::now();
    a.assignedBy = "user";
    
    pImpl->assignments.push_back(a);
    pImpl->fileToTags[filePath].insert(tagId);
    pImpl->tagToFiles[tagId].insert(filePath);
    
    // Update usage count
    auto* tag = getTag(tagId);
    if (tag) {
        tag->usageCount = static_cast<int>(pImpl->tagToFiles[tagId].size());
    }
    
    pImpl->notifyEvent(TagEventType::TagAssigned, tagId, filePath);
    save();
    
    return true;
}

bool TagManager::removeTag(const std::string& filePath, const std::string& tagId) {
    if (!hasTag(filePath, tagId)) {
        return false;
    }
    
    pImpl->assignments.erase(
        std::remove_if(pImpl->assignments.begin(), pImpl->assignments.end(),
            [&](const TagAssignment& a) { 
                return a.filePath == filePath && a.tagId == tagId; 
            }),
        pImpl->assignments.end());
    
    pImpl->fileToTags[filePath].erase(tagId);
    pImpl->tagToFiles[tagId].erase(filePath);
    
    // Update usage count
    auto* tag = getTag(tagId);
    if (tag) {
        tag->usageCount = static_cast<int>(pImpl->tagToFiles[tagId].size());
    }
    
    pImpl->notifyEvent(TagEventType::TagRemoved, tagId, filePath);
    save();
    
    return true;
}

bool TagManager::toggleTag(const std::string& filePath, const std::string& tagId) {
    if (hasTag(filePath, tagId)) {
        return removeTag(filePath, tagId);
    } else {
        return assignTag(filePath, tagId);
    }
}

bool TagManager::setTags(const std::string& filePath, const std::vector<std::string>& tagIds) {
    // Remove all existing tags
    clearTags(filePath);
    
    // Add new tags
    for (const auto& tagId : tagIds) {
        assignTag(filePath, tagId);
    }
    
    return true;
}

bool TagManager::clearTags(const std::string& filePath) {
    auto it = pImpl->fileToTags.find(filePath);
    if (it == pImpl->fileToTags.end() || it->second.empty()) {
        return true;
    }
    
    std::set<std::string> tagsToRemove = it->second;
    for (const auto& tagId : tagsToRemove) {
        removeTag(filePath, tagId);
    }
    
    return true;
}

bool TagManager::hasTag(const std::string& filePath, const std::string& tagId) const {
    auto it = pImpl->fileToTags.find(filePath);
    if (it == pImpl->fileToTags.end()) {
        return false;
    }
    return it->second.count(tagId) > 0;
}

bool TagManager::assignTagToMany(const std::vector<std::string>& filePaths, const std::string& tagId) {
    for (const auto& path : filePaths) {
        assignTag(path, tagId);
    }
    return true;
}

bool TagManager::removeTagFromMany(const std::vector<std::string>& filePaths, const std::string& tagId) {
    for (const auto& path : filePaths) {
        removeTag(path, tagId);
    }
    return true;
}

bool TagManager::copyTags(const std::string& sourcePath, const std::string& destPath) {
    auto tags = getTagsForFile(sourcePath);
    return setTags(destPath, tags);
}

std::vector<std::string> TagManager::getTagsForFile(const std::string& filePath) const {
    std::vector<std::string> result;
    auto it = pImpl->fileToTags.find(filePath);
    if (it != pImpl->fileToTags.end()) {
        result.assign(it->second.begin(), it->second.end());
    }
    return result;
}

std::vector<Tag> TagManager::getTagObjectsForFile(const std::string& filePath) const {
    std::vector<Tag> result;
    auto tagIds = getTagsForFile(filePath);
    for (const auto& id : tagIds) {
        auto* tag = getTag(id);
        if (tag) {
            result.push_back(*tag);
        }
    }
    return result;
}

std::vector<std::string> TagManager::getFilesWithTag(const std::string& tagId) const {
    std::vector<std::string> result;
    auto it = pImpl->tagToFiles.find(tagId);
    if (it != pImpl->tagToFiles.end()) {
        result.assign(it->second.begin(), it->second.end());
    }
    return result;
}

std::vector<std::string> TagManager::getFilesMatchingFilter(const TagFilter& filter) const {
    std::set<std::string> result;
    bool first = true;
    
    // Must have ALL includeTags
    for (const auto& tagId : filter.includeTags) {
        auto files = getFilesWithTag(tagId);
        std::set<std::string> fileSet(files.begin(), files.end());
        
        if (first) {
            result = fileSet;
            first = false;
        } else {
            std::set<std::string> intersection;
            std::set_intersection(result.begin(), result.end(),
                                 fileSet.begin(), fileSet.end(),
                                 std::inserter(intersection, intersection.begin()));
            result = intersection;
        }
    }
    
    // Must have ANY of anyOfTags
    if (!filter.anyOfTags.empty()) {
        std::set<std::string> anyMatch;
        for (const auto& tagId : filter.anyOfTags) {
            auto files = getFilesWithTag(tagId);
            anyMatch.insert(files.begin(), files.end());
        }
        
        if (first) {
            result = anyMatch;
            first = false;
        } else {
            std::set<std::string> intersection;
            std::set_intersection(result.begin(), result.end(),
                                 anyMatch.begin(), anyMatch.end(),
                                 std::inserter(intersection, intersection.begin()));
            result = intersection;
        }
    }
    
    // Must NOT have excludeTags
    for (const auto& tagId : filter.excludeTags) {
        auto files = getFilesWithTag(tagId);
        for (const auto& f : files) {
            result.erase(f);
        }
    }
    
    return std::vector<std::string>(result.begin(), result.end());
}

int TagManager::getFileCountForTag(const std::string& tagId) const {
    auto it = pImpl->tagToFiles.find(tagId);
    return it != pImpl->tagToFiles.end() ? static_cast<int>(it->second.size()) : 0;
}

std::string TagManager::addRule(const SmartTagRule& rule) {
    SmartTagRule r = rule;
    if (r.id.empty()) {
        r.id = GenerateUUID();
    }
    
    pImpl->rules.push_back(r);
    save();
    
    return r.id;
}

bool TagManager::deleteRule(const std::string& ruleId) {
    auto it = std::find_if(pImpl->rules.begin(), pImpl->rules.end(),
        [&](const SmartTagRule& r) { return r.id == ruleId; });
    
    if (it != pImpl->rules.end()) {
        pImpl->rules.erase(it);
        save();
        return true;
    }
    
    return false;
}

bool TagManager::updateRule(const std::string& ruleId, const SmartTagRule& updated) {
    for (auto& r : pImpl->rules) {
        if (r.id == ruleId) {
            r = updated;
            r.id = ruleId;
            save();
            return true;
        }
    }
    return false;
}

SmartTagRule* TagManager::getRule(const std::string& ruleId) {
    for (auto& r : pImpl->rules) {
        if (r.id == ruleId) {
            return &r;
        }
    }
    return nullptr;
}

std::vector<const SmartTagRule*> TagManager::getAllRules() const {
    std::vector<const SmartTagRule*> result;
    for (const auto& r : pImpl->rules) {
        result.push_back(&r);
    }
    return result;
}

void TagManager::applyRules(const std::string& filePath) {
    for (const auto& rule : pImpl->rules) {
        if (pImpl->matchesRule(rule, filePath)) {
            assignTag(filePath, rule.tagId);
            pImpl->notifyEvent(TagEventType::RuleTriggered, rule.tagId, filePath, rule.name);
        }
    }
}

void TagManager::applyAllRules() {
    // Apply rules to all files that have at least one tag
    std::set<std::string> allFiles;
    for (const auto& [path, tags] : pImpl->fileToTags) {
        allFiles.insert(path);
    }
    
    for (const auto& path : allFiles) {
        applyRules(path);
    }
}

std::vector<const Tag*> TagManager::suggestTags(const std::string& filePath) const {
    std::vector<const Tag*> suggestions;
    
    // Suggest based on extension
    std::string ext = fs::path(filePath).extension().string();
    
    // Suggest frequently used tags
    auto frequent = getFrequentTags(3);
    suggestions.insert(suggestions.end(), frequent.begin(), frequent.end());
    
    // Suggest recently used tags
    auto recent = getRecentlyUsedTags(2);
    for (const auto* tag : recent) {
        if (std::find(suggestions.begin(), suggestions.end(), tag) == suggestions.end()) {
            suggestions.push_back(tag);
        }
    }
    
    return suggestions;
}

std::vector<const Tag*> TagManager::getFrequentTags(int maxCount) const {
    auto tags = getTagsByUsage(1);
    if (tags.size() > static_cast<size_t>(maxCount)) {
        tags.resize(maxCount);
    }
    return tags;
}

std::vector<const Tag*> TagManager::getRecentlyUsedTags(int maxCount) const {
    // Find most recently assigned tags
    std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> recentAssignments;
    
    for (const auto& a : pImpl->assignments) {
        recentAssignments.emplace_back(a.tagId, a.assignedAt);
    }
    
    std::sort(recentAssignments.begin(), recentAssignments.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::vector<const Tag*> result;
    std::set<std::string> seen;
    
    for (const auto& [tagId, time] : recentAssignments) {
        if (seen.insert(tagId).second) {
            if (auto* tag = getTag(tagId)) {
                result.push_back(tag);
                if (result.size() >= static_cast<size_t>(maxCount)) {
                    break;
                }
            }
        }
    }
    
    return result;
}

bool TagManager::exportTags(const std::string& filePath) const {
    try {
        json j;
        j["version"] = 1;
        j["exportedAt"] = TimePointToString(std::chrono::system_clock::now());
        j["tags"] = pImpl->tags;
        j["assignments"] = pImpl->assignments;
        j["rules"] = pImpl->rules;
        
        std::ofstream file(filePath);
        if (!file) {
            return false;
        }
        
        file << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool TagManager::importTags(const std::string& filePath, bool merge) {
    try {
        std::ifstream file(filePath);
        if (!file) {
            return false;
        }
        
        json j = json::parse(file);
        
        if (!merge) {
            pImpl->tags.clear();
            pImpl->assignments.clear();
            pImpl->rules.clear();
        }
        
        if (j.contains("tags")) {
            auto importedTags = j["tags"].get<std::vector<Tag>>();
            for (auto& t : importedTags) {
                if (!getTagByName(t.name)) {
                    t.id = GenerateUUID();
                    pImpl->tags.push_back(t);
                }
            }
        }
        
        pImpl->rebuildIndex();
        save();
        
        return true;
    } catch (...) {
        return false;
    }
}

bool TagManager::rebuildIndex() {
    pImpl->rebuildIndex();
    return true;
}

int TagManager::cleanupOrphanedAssignments() {
    int count = 0;
    pImpl->assignments.erase(
        std::remove_if(pImpl->assignments.begin(), pImpl->assignments.end(),
            [&count](const TagAssignment& a) {
                if (!fs::exists(a.filePath)) {
                    count++;
                    return true;
                }
                return false;
            }),
        pImpl->assignments.end());
    
    if (count > 0) {
        pImpl->rebuildIndex();
        save();
    }
    
    return count;
}

int TagManager::getOrphanedCount() const {
    int count = 0;
    for (const auto& a : pImpl->assignments) {
        if (!fs::exists(a.filePath)) {
            count++;
        }
    }
    return count;
}

bool TagManager::save() const {
    return pImpl->saveToFile();
}

bool TagManager::load() {
    return pImpl->loadFromFile();
}

void TagManager::addEventCallback(EventCallback callback) {
    pImpl->callbacks.push_back(std::move(callback));
}

size_t TagManager::getTagCount() const {
    return pImpl->tags.size();
}

size_t TagManager::getAssignmentCount() const {
    return pImpl->assignments.size();
}

size_t TagManager::getRuleCount() const {
    return pImpl->rules.size();
}

std::vector<std::pair<std::string, int>> TagManager::getTagUsageStats() const {
    std::vector<std::pair<std::string, int>> stats;
    for (const auto& t : pImpl->tags) {
        stats.emplace_back(t.name, t.usageCount);
    }
    std::sort(stats.begin(), stats.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    return stats;
}

}} // namespace opacity::core
