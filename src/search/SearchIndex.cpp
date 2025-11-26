#include "opacity/search/SearchIndex.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <queue>
#include <regex>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace opacity::search
{
    using namespace opacity::core;
    using json = nlohmann::json;

    // Simple hash function for content change detection
    static uint32_t HashContent(const std::string& content)
    {
        uint32_t hash = 0;
        for (char c : content) {
            hash = hash * 31 + static_cast<uint32_t>(c);
        }
        return hash;
    }

    // Check if file is a text file based on extension
    static bool IsTextFile(const std::filesystem::path& path)
    {
        static const std::vector<std::string> textExtensions = {
            ".txt", ".md", ".json", ".xml", ".html", ".htm", ".css", ".js",
            ".ts", ".cpp", ".c", ".h", ".hpp", ".py", ".java", ".cs", ".go",
            ".rs", ".rb", ".php", ".sh", ".bat", ".ps1", ".yaml", ".yml",
            ".toml", ".ini", ".cfg", ".conf", ".log", ".sql", ".cmake"
        };
        
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        return std::find(textExtensions.begin(), textExtensions.end(), ext) != textExtensions.end();
    }

    class SearchIndex::Impl
    {
    public:
        IndexConfig config_;
        IndexStats stats_;
        
        std::unordered_map<std::string, IndexEntry> entries_;
        mutable std::shared_mutex entriesMutex_;
        
        std::vector<IndexUpdateCallback> updateCallbacks_;
        
        std::atomic<bool> initialized_{false};
        std::atomic<bool> indexing_{false};
        std::atomic<bool> searching_{false};
        std::atomic<bool> cancelIndexing_{false};
        std::atomic<bool> cancelSearch_{false};
        std::atomic<double> indexingProgress_{0.0};
        
        std::thread autoUpdateThread_;
        std::atomic<bool> autoUpdateRunning_{false};
        
        void NotifyUpdate(const IndexUpdateEvent& event)
        {
            for (auto& callback : updateCallbacks_) {
                if (callback) callback(event);
            }
        }

        bool ShouldIndex(const std::filesystem::path& path)
        {
            std::string filename = path.filename().string();
            
            // Check excluded directories
            for (const auto& excluded : config_.excludedDirs) {
                if (path.string().find(excluded) != std::string::npos) {
                    return false;
                }
            }

            // Check hidden files
            if (!config_.indexHiddenFiles && filename[0] == '.') {
                return false;
            }

            // Check extensions if filtering
            if (!config_.includedExtensions.empty()) {
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                bool found = false;
                for (const auto& included : config_.includedExtensions) {
                    std::string incLower = included;
                    std::transform(incLower.begin(), incLower.end(), incLower.begin(), ::tolower);
                    if (ext == incLower || ext == "." + incLower) {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }

            // Check excluded extensions
            if (!config_.excludedExtensions.empty()) {
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                for (const auto& excluded : config_.excludedExtensions) {
                    std::string exLower = excluded;
                    std::transform(exLower.begin(), exLower.end(), exLower.begin(), ::tolower);
                    if (ext == exLower || ext == "." + exLower) {
                        return false;
                    }
                }
            }

            return true;
        }

        IndexEntry CreateEntry(const std::filesystem::path& path)
        {
            IndexEntry entry;
            entry.path = path;
            entry.filename = path.filename().string();
            entry.extension = path.extension().string();
            entry.indexedTime = std::chrono::system_clock::now();
            
            std::error_code ec;
            
            if (std::filesystem::is_directory(path, ec)) {
                entry.isDirectory = true;
                return entry;
            }

            entry.size = std::filesystem::file_size(path, ec);
            
            auto ftime = std::filesystem::last_write_time(path, ec);
            if (!ec) {
                // Convert file_time_type to system_clock::time_point
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                entry.modifiedTime = sctp;
            }

            // Index content for text files
            if (config_.indexContent && 
                IsTextFile(path) && 
                entry.size <= config_.maxFileSize)
            {
                try {
                    std::ifstream file(path, std::ios::binary);
                    if (file) {
                        std::stringstream ss;
                        ss << file.rdbuf();
                        entry.content = ss.str();
                        entry.contentHash = HashContent(entry.content);
                    }
                }
                catch (...) {
                    // Ignore content indexing errors
                }
            }

            return entry;
        }

        void IndexDirectory(const std::filesystem::path& root, 
                           std::vector<IndexEntry>& newEntries,
                           IndexProgressCallback progress)
        {
            std::error_code ec;
            
            // Count files first for progress
            size_t totalFiles = 0;
            size_t processedFiles = 0;
            
            for (auto& entry : std::filesystem::recursive_directory_iterator(root, 
                std::filesystem::directory_options::skip_permission_denied, ec))
            {
                if (cancelIndexing_) return;
                totalFiles++;
            }

            // Now index
            for (auto& entry : std::filesystem::recursive_directory_iterator(root,
                std::filesystem::directory_options::skip_permission_denied, ec))
            {
                if (cancelIndexing_) return;

                const auto& path = entry.path();
                
                if (!ShouldIndex(path)) {
                    continue;
                }

                try {
                    IndexEntry indexEntry = CreateEntry(path);
                    newEntries.push_back(std::move(indexEntry));
                }
                catch (...) {
                    // Skip problematic files
                }

                processedFiles++;
                if (progress && totalFiles > 0) {
                    double prog = static_cast<double>(processedFiles) / totalFiles;
                    indexingProgress_ = prog;
                    progress(path.string(), prog);
                }
            }
        }

        float CalculateScore(const IndexEntry& entry, const SearchQuery& query)
        {
            float score = 0.0f;
            std::string searchText = query.text;
            
            if (!query.caseSensitive) {
                std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::tolower);
            }

            // Filename match (highest weight)
            if (query.searchFilenames) {
                std::string filename = entry.filename;
                if (!query.caseSensitive) {
                    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
                }

                if (filename == searchText) {
                    score += 1.0f;  // Exact match
                } else if (filename.find(searchText) == 0) {
                    score += 0.8f;  // Prefix match
                } else if (filename.find(searchText) != std::string::npos) {
                    score += 0.5f;  // Substring match
                }
            }

            // Content match
            if (query.searchContent && !entry.content.empty()) {
                std::string content = entry.content;
                if (!query.caseSensitive) {
                    std::transform(content.begin(), content.end(), content.begin(), ::tolower);
                }

                size_t pos = 0;
                int matchCount = 0;
                while ((pos = content.find(searchText, pos)) != std::string::npos) {
                    matchCount++;
                    pos += searchText.length();
                }

                if (matchCount > 0) {
                    // More matches = higher score, but with diminishing returns
                    score += 0.3f * std::min(1.0f, static_cast<float>(matchCount) / 10.0f);
                }
            }

            return score;
        }

        std::vector<std::pair<size_t, size_t>> FindMatches(const std::string& text, 
                                                           const SearchQuery& query)
        {
            std::vector<std::pair<size_t, size_t>> matches;
            
            std::string searchText = query.text;
            std::string content = text;
            
            if (!query.caseSensitive) {
                std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::tolower);
                std::transform(content.begin(), content.end(), content.begin(), ::tolower);
            }

            if (query.useRegex) {
                try {
                    auto flags = std::regex::ECMAScript;
                    if (!query.caseSensitive) flags |= std::regex::icase;
                    
                    std::regex re(searchText, flags);
                    auto begin = std::sregex_iterator(content.begin(), content.end(), re);
                    auto end = std::sregex_iterator();
                    
                    for (auto it = begin; it != end; ++it) {
                        matches.emplace_back(it->position(), it->length());
                    }
                }
                catch (...) {
                    // Invalid regex, fall back to simple search
                }
            }

            if (matches.empty()) {
                size_t pos = 0;
                while ((pos = content.find(searchText, pos)) != std::string::npos) {
                    matches.emplace_back(pos, searchText.length());
                    pos += searchText.length();
                }
            }

            return matches;
        }

        std::string GetMatchContext(const std::string& content, 
                                    const std::vector<std::pair<size_t, size_t>>& matches,
                                    int contextChars = 50)
        {
            if (matches.empty() || content.empty()) return "";

            auto& firstMatch = matches[0];
            size_t start = firstMatch.first > static_cast<size_t>(contextChars) ? 
                firstMatch.first - contextChars : 0;
            size_t end = std::min(content.length(), 
                firstMatch.first + firstMatch.second + contextChars);

            std::string context = content.substr(start, end - start);
            
            // Clean up whitespace
            std::replace(context.begin(), context.end(), '\n', ' ');
            std::replace(context.begin(), context.end(), '\r', ' ');
            std::replace(context.begin(), context.end(), '\t', ' ');

            // Add ellipsis if truncated
            if (start > 0) context = "..." + context;
            if (end < content.length()) context = context + "...";

            return context;
        }
    };

    // ============== SearchIndex ==============

    SearchIndex::SearchIndex()
        : impl_(std::make_unique<Impl>())
    {}

    SearchIndex::~SearchIndex()
    {
        Shutdown();
    }

    SearchIndex::SearchIndex(SearchIndex&&) noexcept = default;
    SearchIndex& SearchIndex::operator=(SearchIndex&&) noexcept = default;

    bool SearchIndex::Initialize(const IndexConfig& config)
    {
        impl_->config_ = config;

        // Create index directory if needed
        std::error_code ec;
        if (!config.indexPath.empty() && !std::filesystem::exists(config.indexPath, ec)) {
            std::filesystem::create_directories(config.indexPath, ec);
        }

        impl_->initialized_ = true;
        Logger::Get()->info("SearchIndex: Initialized with {} roots", config.roots.size());
        return true;
    }

    void SearchIndex::Shutdown()
    {
        StopAutoUpdate();
        CancelIndexing();
        CancelSearch();
        
        SaveIndex();
        
        impl_->initialized_ = false;
        Logger::Get()->info("SearchIndex: Shutdown");
    }

    bool SearchIndex::IsInitialized() const
    {
        return impl_->initialized_;
    }

    bool SearchIndex::RebuildIndex(IndexProgressCallback progress)
    {
        if (impl_->indexing_) {
            return false;
        }

        impl_->indexing_ = true;
        impl_->cancelIndexing_ = false;
        impl_->indexingProgress_ = 0.0;

        auto startTime = std::chrono::steady_clock::now();

        impl_->NotifyUpdate({IndexUpdateEvent::Type::Started, {}, "Index rebuild started"});

        {
            std::unique_lock<std::shared_mutex> lock(impl_->entriesMutex_);
            impl_->entries_.clear();
        }

        std::vector<IndexEntry> newEntries;
        
        for (const auto& root : impl_->config_.roots) {
            if (impl_->cancelIndexing_) break;
            
            std::error_code ec;
            if (std::filesystem::exists(root, ec)) {
                impl_->IndexDirectory(root, newEntries, progress);
            }
        }

        if (!impl_->cancelIndexing_) {
            std::unique_lock<std::shared_mutex> lock(impl_->entriesMutex_);
            
            for (auto& entry : newEntries) {
                impl_->entries_[entry.path.string()] = std::move(entry);
            }

            impl_->stats_.totalFiles = 0;
            impl_->stats_.totalDirectories = 0;
            impl_->stats_.indexedFiles = impl_->entries_.size();
            impl_->stats_.contentIndexedFiles = 0;
            impl_->stats_.totalSizeBytes = 0;

            for (const auto& [path, entry] : impl_->entries_) {
                if (entry.isDirectory) {
                    impl_->stats_.totalDirectories++;
                } else {
                    impl_->stats_.totalFiles++;
                    impl_->stats_.totalSizeBytes += entry.size;
                    if (!entry.content.empty()) {
                        impl_->stats_.contentIndexedFiles++;
                    }
                }
            }

            auto endTime = std::chrono::steady_clock::now();
            impl_->stats_.lastUpdate = std::chrono::system_clock::now();
            impl_->stats_.lastUpdateDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime);
        }

        impl_->indexing_ = false;
        impl_->indexingProgress_ = 1.0;

        impl_->NotifyUpdate({IndexUpdateEvent::Type::Completed, {}, 
            "Index rebuild completed: " + std::to_string(impl_->entries_.size()) + " entries"});

        Logger::Get()->info("SearchIndex: Rebuilt index with {} entries in {}ms",
            impl_->entries_.size(), impl_->stats_.lastUpdateDuration.count());

        return !impl_->cancelIndexing_;
    }

    bool SearchIndex::UpdateIndex(IndexProgressCallback progress)
    {
        // For now, just rebuild - a full implementation would do incremental updates
        return RebuildIndex(progress);
    }

    bool SearchIndex::AddToIndex(const std::filesystem::path& path)
    {
        if (!impl_->ShouldIndex(path)) {
            return false;
        }

        try {
            IndexEntry entry = impl_->CreateEntry(path);
            
            std::unique_lock<std::shared_mutex> lock(impl_->entriesMutex_);
            impl_->entries_[path.string()] = std::move(entry);
            
            impl_->NotifyUpdate({IndexUpdateEvent::Type::Added, path, ""});
            return true;
        }
        catch (const std::exception& e) {
            Logger::Get()->error("SearchIndex: Failed to add {}: {}", path.string(), e.what());
            return false;
        }
    }

    bool SearchIndex::RemoveFromIndex(const std::filesystem::path& path)
    {
        std::unique_lock<std::shared_mutex> lock(impl_->entriesMutex_);
        
        auto it = impl_->entries_.find(path.string());
        if (it != impl_->entries_.end()) {
            impl_->entries_.erase(it);
            impl_->NotifyUpdate({IndexUpdateEvent::Type::Removed, path, ""});
            return true;
        }
        return false;
    }

    bool SearchIndex::UpdateEntry(const std::filesystem::path& path)
    {
        RemoveFromIndex(path);
        return AddToIndex(path);
    }

    void SearchIndex::CancelIndexing()
    {
        impl_->cancelIndexing_ = true;
    }

    bool SearchIndex::IsIndexing() const
    {
        return impl_->indexing_;
    }

    double SearchIndex::GetIndexingProgress() const
    {
        return impl_->indexingProgress_;
    }

    std::vector<SearchResult> SearchIndex::Search(const SearchQuery& query)
    {
        auto startTime = std::chrono::steady_clock::now();
        std::vector<SearchResult> results;

        impl_->searching_ = true;
        impl_->cancelSearch_ = false;

        {
            std::shared_lock<std::shared_mutex> lock(impl_->entriesMutex_);

            for (const auto& [path, entry] : impl_->entries_) {
                if (impl_->cancelSearch_) break;
                if (results.size() >= static_cast<size_t>(query.maxResults)) break;

                // Apply filters
                if (!query.extensions.empty()) {
                    std::string ext = entry.extension;
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    bool found = false;
                    for (const auto& qext : query.extensions) {
                        std::string qextLower = qext;
                        std::transform(qextLower.begin(), qextLower.end(), qextLower.begin(), ::tolower);
                        if (ext == qextLower || ext == "." + qextLower) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) continue;
                }

                if (query.minSize && entry.size < *query.minSize) continue;
                if (query.maxSize && entry.size > *query.maxSize) continue;
                if (query.modifiedAfter && entry.modifiedTime < *query.modifiedAfter) continue;
                if (query.modifiedBefore && entry.modifiedTime > *query.modifiedBefore) continue;

                // Calculate score
                float score = impl_->CalculateScore(entry, query);
                
                if (score > 0.0f) {
                    SearchResult result;
                    result.entry = entry;
                    result.score = score;
                    
                    if (query.searchContent && !entry.content.empty()) {
                        result.matches = impl_->FindMatches(entry.content, query);
                        result.matchContext = impl_->GetMatchContext(entry.content, result.matches);
                    }
                    
                    results.push_back(std::move(result));
                }
            }
        }

        // Sort by score descending
        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) {
                return a.score > b.score;
            });

        auto endTime = std::chrono::steady_clock::now();
        impl_->stats_.lastSearchDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime);

        impl_->searching_ = false;

        Logger::Get()->debug("SearchIndex: Found {} results in {}ms",
            results.size(), impl_->stats_.lastSearchDuration.count());

        return results;
    }

    void SearchIndex::SearchAsync(const SearchQuery& query, SearchResultCallback callback)
    {
        std::thread([this, query, callback]() {
            auto results = Search(query);
            for (const auto& result : results) {
                if (impl_->cancelSearch_) break;
                callback(result);
            }
        }).detach();
    }

    void SearchIndex::CancelSearch()
    {
        impl_->cancelSearch_ = true;
    }

    std::vector<std::filesystem::path> SearchIndex::QuickSearch(const std::string& pattern, int maxResults)
    {
        std::vector<std::filesystem::path> results;
        
        std::string patternLower = pattern;
        std::transform(patternLower.begin(), patternLower.end(), patternLower.begin(), ::tolower);

        std::shared_lock<std::shared_mutex> lock(impl_->entriesMutex_);

        for (const auto& [path, entry] : impl_->entries_) {
            if (static_cast<int>(results.size()) >= maxResults) break;

            std::string filename = entry.filename;
            std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);

            if (filename.find(patternLower) != std::string::npos) {
                results.push_back(entry.path);
            }
        }

        return results;
    }

    bool SearchIndex::IsIndexed(const std::filesystem::path& path) const
    {
        std::shared_lock<std::shared_mutex> lock(impl_->entriesMutex_);
        return impl_->entries_.find(path.string()) != impl_->entries_.end();
    }

    std::optional<IndexEntry> SearchIndex::GetEntry(const std::filesystem::path& path) const
    {
        std::shared_lock<std::shared_mutex> lock(impl_->entriesMutex_);
        
        auto it = impl_->entries_.find(path.string());
        if (it != impl_->entries_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void SearchIndex::ClearIndex()
    {
        std::unique_lock<std::shared_mutex> lock(impl_->entriesMutex_);
        impl_->entries_.clear();
        impl_->stats_ = IndexStats{};
        Logger::Get()->info("SearchIndex: Cleared index");
    }

    bool SearchIndex::OptimizeIndex()
    {
        // No-op for in-memory index
        return true;
    }

    bool SearchIndex::CompactIndex()
    {
        // No-op for in-memory index
        return true;
    }

    bool SearchIndex::SaveIndex()
    {
        if (impl_->config_.indexPath.empty()) {
            return false;
        }

        try {
            std::filesystem::path indexFile = impl_->config_.indexPath / "index.json";
            
            json j;
            j["version"] = 1;
            j["stats"]["totalFiles"] = impl_->stats_.totalFiles;
            j["stats"]["indexedFiles"] = impl_->stats_.indexedFiles;
            
            std::shared_lock<std::shared_mutex> lock(impl_->entriesMutex_);
            
            json entries = json::array();
            for (const auto& [path, entry] : impl_->entries_) {
                json e;
                e["path"] = entry.path.string();
                e["filename"] = entry.filename;
                e["extension"] = entry.extension;
                e["size"] = entry.size;
                e["isDirectory"] = entry.isDirectory;
                e["contentHash"] = entry.contentHash;
                // Don't save content - too large
                entries.push_back(e);
            }
            j["entries"] = entries;

            std::ofstream file(indexFile);
            file << j.dump();

            Logger::Get()->info("SearchIndex: Saved index to {}", indexFile.string());
            return true;
        }
        catch (const std::exception& e) {
            Logger::Get()->error("SearchIndex: Failed to save index: {}", e.what());
            return false;
        }
    }

    bool SearchIndex::LoadIndex()
    {
        if (impl_->config_.indexPath.empty()) {
            return false;
        }

        try {
            std::filesystem::path indexFile = impl_->config_.indexPath / "index.json";
            
            std::error_code ec;
            if (!std::filesystem::exists(indexFile, ec)) {
                return false;
            }

            std::ifstream file(indexFile);
            json j = json::parse(file);

            std::unique_lock<std::shared_mutex> lock(impl_->entriesMutex_);
            impl_->entries_.clear();

            for (const auto& e : j["entries"]) {
                IndexEntry entry;
                entry.path = std::filesystem::path(e["path"].get<std::string>());
                entry.filename = e["filename"].get<std::string>();
                entry.extension = e["extension"].get<std::string>();
                entry.size = e["size"].get<uint64_t>();
                entry.isDirectory = e["isDirectory"].get<bool>();
                entry.contentHash = e.value("contentHash", 0u);
                
                impl_->entries_[entry.path.string()] = std::move(entry);
            }

            impl_->stats_.indexedFiles = impl_->entries_.size();

            Logger::Get()->info("SearchIndex: Loaded {} entries from index", impl_->entries_.size());
            return true;
        }
        catch (const std::exception& e) {
            Logger::Get()->error("SearchIndex: Failed to load index: {}", e.what());
            return false;
        }
    }

    bool SearchIndex::ExportIndex(const std::filesystem::path& exportPath)
    {
        auto originalPath = impl_->config_.indexPath;
        impl_->config_.indexPath = exportPath.parent_path();
        bool result = SaveIndex();
        impl_->config_.indexPath = originalPath;
        return result;
    }

    bool SearchIndex::ImportIndex(const std::filesystem::path& importPath)
    {
        auto originalPath = impl_->config_.indexPath;
        impl_->config_.indexPath = importPath.parent_path();
        bool result = LoadIndex();
        impl_->config_.indexPath = originalPath;
        return result;
    }

    IndexStats SearchIndex::GetStatistics() const
    {
        return impl_->stats_;
    }

    std::vector<std::filesystem::path> SearchIndex::GetIndexedRoots() const
    {
        return impl_->config_.roots;
    }

    bool SearchIndex::VerifyIndex()
    {
        // Check that indexed files still exist
        int missingCount = 0;
        
        std::shared_lock<std::shared_mutex> lock(impl_->entriesMutex_);
        
        for (const auto& [path, entry] : impl_->entries_) {
            std::error_code ec;
            if (!std::filesystem::exists(entry.path, ec)) {
                missingCount++;
            }
        }

        Logger::Get()->info("SearchIndex: Verified index, {} missing files", missingCount);
        return missingCount == 0;
    }

    const IndexConfig& SearchIndex::GetConfig() const
    {
        return impl_->config_;
    }

    void SearchIndex::SetConfig(const IndexConfig& config)
    {
        impl_->config_ = config;
    }

    void SearchIndex::AddRoot(const std::filesystem::path& root)
    {
        impl_->config_.roots.push_back(root);
    }

    void SearchIndex::RemoveRoot(const std::filesystem::path& root)
    {
        auto& roots = impl_->config_.roots;
        roots.erase(std::remove(roots.begin(), roots.end(), root), roots.end());
    }

    void SearchIndex::StartAutoUpdate()
    {
        if (impl_->autoUpdateRunning_) return;

        impl_->autoUpdateRunning_ = true;
        impl_->autoUpdateThread_ = std::thread([this]() {
            while (impl_->autoUpdateRunning_) {
                std::this_thread::sleep_for(
                    std::chrono::seconds(impl_->config_.updateIntervalSeconds));
                
                if (impl_->autoUpdateRunning_ && !impl_->indexing_) {
                    UpdateIndex();
                }
            }
        });

        Logger::Get()->info("SearchIndex: Auto-update started");
    }

    void SearchIndex::StopAutoUpdate()
    {
        impl_->autoUpdateRunning_ = false;
        
        if (impl_->autoUpdateThread_.joinable()) {
            impl_->autoUpdateThread_.join();
        }

        Logger::Get()->info("SearchIndex: Auto-update stopped");
    }

    bool SearchIndex::IsAutoUpdateRunning() const
    {
        return impl_->autoUpdateRunning_;
    }

    void SearchIndex::OnIndexUpdate(IndexUpdateCallback callback)
    {
        impl_->updateCallbacks_.push_back(callback);
    }

    // ============== SearchQueryBuilder ==============

    SearchQueryBuilder& SearchQueryBuilder::Text(const std::string& text)
    {
        query_.text = text;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::CaseSensitive(bool enable)
    {
        query_.caseSensitive = enable;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::WholeWord(bool enable)
    {
        query_.wholeWord = enable;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::UseRegex(bool enable)
    {
        query_.useRegex = enable;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::SearchContent(bool enable)
    {
        query_.searchContent = enable;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::SearchFilenames(bool enable)
    {
        query_.searchFilenames = enable;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::WithExtensions(const std::vector<std::string>& exts)
    {
        query_.extensions = exts;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::MinSize(uint64_t bytes)
    {
        query_.minSize = bytes;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::MaxSize(uint64_t bytes)
    {
        query_.maxSize = bytes;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::ModifiedAfter(std::chrono::system_clock::time_point time)
    {
        query_.modifiedAfter = time;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::ModifiedBefore(std::chrono::system_clock::time_point time)
    {
        query_.modifiedBefore = time;
        return *this;
    }

    SearchQueryBuilder& SearchQueryBuilder::MaxResults(int count)
    {
        query_.maxResults = count;
        return *this;
    }

    SearchQuery SearchQueryBuilder::Build() const
    {
        return query_;
    }

} // namespace opacity::search
