#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opacity::search
{
    /**
     * @brief Index entry for a single file
     */
    struct IndexEntry
    {
        std::filesystem::path path;
        std::string filename;
        std::string extension;
        std::string content;            // Indexed content (if text file)
        uint64_t size = 0;
        std::chrono::system_clock::time_point modifiedTime;
        std::chrono::system_clock::time_point indexedTime;
        uint32_t contentHash = 0;       // Hash of content for change detection
        bool isDirectory = false;
    };

    /**
     * @brief Search result with relevance scoring
     */
    struct SearchResult
    {
        IndexEntry entry;
        float score = 0.0f;             // Relevance score (0-1)
        std::vector<std::pair<size_t, size_t>> matches;  // Match positions in content
        std::string matchContext;       // Context around match
    };

    /**
     * @brief Search query options
     */
    struct SearchQuery
    {
        std::string text;               // Search text
        bool caseSensitive = false;
        bool wholeWord = false;
        bool useRegex = false;
        bool searchContent = true;      // Search in file content
        bool searchFilenames = true;    // Search in filenames
        
        // Filters
        std::vector<std::string> extensions;        // Filter by extensions
        std::optional<uint64_t> minSize;
        std::optional<uint64_t> maxSize;
        std::optional<std::chrono::system_clock::time_point> modifiedAfter;
        std::optional<std::chrono::system_clock::time_point> modifiedBefore;
        
        int maxResults = 1000;
    };

    /**
     * @brief Index configuration
     */
    struct IndexConfig
    {
        std::filesystem::path indexPath;            // Where to store the index
        std::vector<std::filesystem::path> roots;   // Directories to index
        std::vector<std::string> includedExtensions;// Only index these extensions (empty = all)
        std::vector<std::string> excludedExtensions;// Never index these
        std::vector<std::string> excludedDirs;      // Directory names to skip
        
        uint64_t maxFileSize = 10 * 1024 * 1024;   // Max file size to index content (10MB)
        bool indexContent = true;                   // Index file content
        bool indexHiddenFiles = false;
        bool followSymlinks = false;
        
        int maxThreads = 4;                         // Indexing threads
        int updateIntervalSeconds = 300;            // Auto-update interval (5 min)
    };

    /**
     * @brief Index statistics
     */
    struct IndexStats
    {
        size_t totalFiles = 0;
        size_t totalDirectories = 0;
        size_t indexedFiles = 0;
        size_t contentIndexedFiles = 0;
        uint64_t totalSizeBytes = 0;
        uint64_t indexSizeBytes = 0;
        std::chrono::system_clock::time_point lastUpdate;
        std::chrono::milliseconds lastUpdateDuration{0};
        std::chrono::milliseconds lastSearchDuration{0};
    };

    /**
     * @brief Index update event
     */
    struct IndexUpdateEvent
    {
        enum class Type { Added, Modified, Removed, Started, Completed, Error };
        Type type;
        std::filesystem::path path;
        std::string message;
    };

    /**
     * @brief Callback types
     */
    using IndexProgressCallback = std::function<void(const std::string& currentPath, double progress)>;
    using IndexUpdateCallback = std::function<void(const IndexUpdateEvent& event)>;
    using SearchResultCallback = std::function<void(const SearchResult& result)>;

    /**
     * @brief Search index manager
     * 
     * Provides:
     * - Full-text content indexing
     * - Fast filename search
     * - Incremental index updates
     * - Background indexing
     * - Index persistence
     */
    class SearchIndex
    {
    public:
        SearchIndex();
        ~SearchIndex();

        // Non-copyable, movable
        SearchIndex(const SearchIndex&) = delete;
        SearchIndex& operator=(const SearchIndex&) = delete;
        SearchIndex(SearchIndex&&) noexcept;
        SearchIndex& operator=(SearchIndex&&) noexcept;

        /**
         * @brief Initialize with configuration
         */
        bool Initialize(const IndexConfig& config);

        /**
         * @brief Shutdown and cleanup
         */
        void Shutdown();

        /**
         * @brief Check if initialized
         */
        bool IsInitialized() const;

        // ============== Indexing ==============

        /**
         * @brief Start full index rebuild
         */
        bool RebuildIndex(IndexProgressCallback progress = nullptr);

        /**
         * @brief Update index incrementally
         */
        bool UpdateIndex(IndexProgressCallback progress = nullptr);

        /**
         * @brief Add a single path to the index
         */
        bool AddToIndex(const std::filesystem::path& path);

        /**
         * @brief Remove a path from the index
         */
        bool RemoveFromIndex(const std::filesystem::path& path);

        /**
         * @brief Update a single entry in the index
         */
        bool UpdateEntry(const std::filesystem::path& path);

        /**
         * @brief Cancel ongoing indexing operation
         */
        void CancelIndexing();

        /**
         * @brief Check if indexing is in progress
         */
        bool IsIndexing() const;

        /**
         * @brief Get indexing progress (0-1)
         */
        double GetIndexingProgress() const;

        // ============== Searching ==============

        /**
         * @brief Search the index
         */
        std::vector<SearchResult> Search(const SearchQuery& query);

        /**
         * @brief Search with streaming results
         */
        void SearchAsync(const SearchQuery& query, SearchResultCallback callback);

        /**
         * @brief Cancel ongoing search
         */
        void CancelSearch();

        /**
         * @brief Quick filename search (no content)
         */
        std::vector<std::filesystem::path> QuickSearch(
            const std::string& pattern,
            int maxResults = 100);

        /**
         * @brief Check if a path is in the index
         */
        bool IsIndexed(const std::filesystem::path& path) const;

        /**
         * @brief Get entry from index
         */
        std::optional<IndexEntry> GetEntry(const std::filesystem::path& path) const;

        // ============== Index Management ==============

        /**
         * @brief Clear the entire index
         */
        void ClearIndex();

        /**
         * @brief Optimize the index for faster searches
         */
        bool OptimizeIndex();

        /**
         * @brief Compact the index to reduce disk space
         */
        bool CompactIndex();

        /**
         * @brief Save index to disk
         */
        bool SaveIndex();

        /**
         * @brief Load index from disk
         */
        bool LoadIndex();

        /**
         * @brief Export index to file
         */
        bool ExportIndex(const std::filesystem::path& exportPath);

        /**
         * @brief Import index from file
         */
        bool ImportIndex(const std::filesystem::path& importPath);

        // ============== Statistics ==============

        /**
         * @brief Get index statistics
         */
        IndexStats GetStatistics() const;

        /**
         * @brief Get indexed roots
         */
        std::vector<std::filesystem::path> GetIndexedRoots() const;

        /**
         * @brief Check index health
         */
        bool VerifyIndex();

        // ============== Configuration ==============

        /**
         * @brief Get current configuration
         */
        const IndexConfig& GetConfig() const;

        /**
         * @brief Update configuration (may require rebuild)
         */
        void SetConfig(const IndexConfig& config);

        /**
         * @brief Add indexing root
         */
        void AddRoot(const std::filesystem::path& root);

        /**
         * @brief Remove indexing root
         */
        void RemoveRoot(const std::filesystem::path& root);

        // ============== Auto-Update ==============

        /**
         * @brief Start automatic index updates
         */
        void StartAutoUpdate();

        /**
         * @brief Stop automatic index updates
         */
        void StopAutoUpdate();

        /**
         * @brief Check if auto-update is running
         */
        bool IsAutoUpdateRunning() const;

        // ============== Callbacks ==============

        /**
         * @brief Register callback for index updates
         */
        void OnIndexUpdate(IndexUpdateCallback callback);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /**
     * @brief Builder for search queries
     */
    class SearchQueryBuilder
    {
    public:
        SearchQueryBuilder& Text(const std::string& text);
        SearchQueryBuilder& CaseSensitive(bool enable = true);
        SearchQueryBuilder& WholeWord(bool enable = true);
        SearchQueryBuilder& UseRegex(bool enable = true);
        SearchQueryBuilder& SearchContent(bool enable = true);
        SearchQueryBuilder& SearchFilenames(bool enable = true);
        SearchQueryBuilder& WithExtensions(const std::vector<std::string>& exts);
        SearchQueryBuilder& MinSize(uint64_t bytes);
        SearchQueryBuilder& MaxSize(uint64_t bytes);
        SearchQueryBuilder& ModifiedAfter(std::chrono::system_clock::time_point time);
        SearchQueryBuilder& ModifiedBefore(std::chrono::system_clock::time_point time);
        SearchQueryBuilder& MaxResults(int count);
        
        SearchQuery Build() const;

    private:
        SearchQuery query_;
    };

} // namespace opacity::search
