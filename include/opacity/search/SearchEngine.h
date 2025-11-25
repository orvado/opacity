#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include "opacity/filesystem/FsItem.h"
#include "opacity/core/Path.h"

namespace opacity::search
{
    /**
     * @brief Search options for controlling search behavior
     */
    struct SearchOptions
    {
        bool case_sensitive = false;
        bool search_contents = false;     // Search file contents (slower)
        bool include_hidden = false;
        bool recursive = true;
        size_t max_results = 1000;
        std::vector<std::string> extensions;  // Filter by extensions (empty = all)
    };

    /**
     * @brief Result of a search operation
     */
    struct SearchResult
    {
        filesystem::FsItem item;
        std::string match_context;  // For content search, shows matching line
        size_t match_line = 0;      // Line number for content matches
    };

    /**
     * @brief Callback for search progress updates
     */
    using SearchProgressCallback = std::function<void(size_t files_searched, size_t matches_found)>;
    
    /**
     * @brief Callback for each search result found
     */
    using SearchResultCallback = std::function<void(const SearchResult& result)>;

    /**
     * @brief Search engine for finding files by name or content
     */
    class SearchEngine
    {
    public:
        SearchEngine();
        ~SearchEngine();

        // Disable copy
        SearchEngine(const SearchEngine&) = delete;
        SearchEngine& operator=(const SearchEngine&) = delete;

        /**
         * @brief Start an asynchronous search
         * @param root_path Starting directory for the search
         * @param query Search query (supports * and ? wildcards)
         * @param options Search options
         * @param result_callback Called for each result found
         * @param progress_callback Called periodically with progress updates
         */
        void StartSearch(
            const core::Path& root_path,
            const std::string& query,
            const SearchOptions& options,
            SearchResultCallback result_callback,
            SearchProgressCallback progress_callback = nullptr);

        /**
         * @brief Cancel the current search operation
         */
        void CancelSearch();

        /**
         * @brief Check if a search is currently running
         */
        bool IsSearching() const;

        /**
         * @brief Wait for the current search to complete
         */
        void WaitForCompletion();

        /**
         * @brief Perform a synchronous search (blocking)
         * @return Vector of all search results
         */
        std::vector<SearchResult> SearchSync(
            const core::Path& root_path,
            const std::string& query,
            const SearchOptions& options);

        /**
         * @brief Simple pattern matching for filename
         * @param filename The filename to check
         * @param pattern The pattern (supports * and ? wildcards)
         * @param case_sensitive Whether matching is case-sensitive
         * @return true if filename matches the pattern
         */
        static bool MatchPattern(
            const std::string& filename,
            const std::string& pattern,
            bool case_sensitive = false);

    private:
        void SearchThread(
            core::Path root_path,
            std::string query,
            SearchOptions options,
            SearchResultCallback result_callback,
            SearchProgressCallback progress_callback);

        void SearchDirectory(
            const core::Path& directory,
            const std::string& query,
            const SearchOptions& options,
            SearchResultCallback result_callback,
            size_t& files_searched,
            size_t& matches_found);

        bool MatchesExtensionFilter(
            const std::string& extension,
            const std::vector<std::string>& extensions) const;

        std::thread search_thread_;
        std::atomic<bool> cancel_requested_{false};
        std::atomic<bool> is_searching_{false};
        mutable std::mutex mutex_;
    };

} // namespace opacity::search
