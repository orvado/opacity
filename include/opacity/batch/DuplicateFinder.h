#pragma once

#include "opacity/core/Path.h"
#include "opacity/filesystem/FsItem.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace opacity::batch
{
    /**
     * @brief How to identify duplicate files
     */
    enum class DuplicateMatchMode
    {
        ExactHash,          // MD5/SHA hash comparison (slowest, most accurate)
        QuickHash,          // Hash of first/last blocks only (faster)
        SizeOnly,           // Same size files (fastest, least accurate)
        SizeAndName,        // Same size and filename
        SizeAndPartialHash  // Same size + hash of first 64KB
    };

    /**
     * @brief Represents a group of duplicate files
     */
    struct DuplicateGroup
    {
        std::string hash;                           // Hash or identifier
        uint64_t file_size = 0;                     // Size of each file
        std::vector<core::Path> files;              // Files in this group
        std::chrono::system_clock::time_point oldest_modified;
        std::chrono::system_clock::time_point newest_modified;
        
        /**
         * @brief Get total wasted space (group size * (count - 1))
         */
        uint64_t GetWastedSpace() const
        {
            return files.size() > 1 ? file_size * (files.size() - 1) : 0;
        }

        /**
         * @brief Get the oldest file in the group
         */
        core::Path GetOldestFile() const;

        /**
         * @brief Get the newest file in the group
         */
        core::Path GetNewestFile() const;

        /**
         * @brief Get file in shortest path
         */
        core::Path GetShortestPath() const;

        /**
         * @brief Get file in longest path
         */
        core::Path GetLongestPath() const;
    };

    /**
     * @brief Options for duplicate search
     */
    struct DuplicateSearchOptions
    {
        DuplicateMatchMode mode = DuplicateMatchMode::SizeAndPartialHash;
        bool recursive = true;
        bool include_hidden = false;
        uint64_t min_size = 0;                      // Minimum file size to check
        uint64_t max_size = 0;                      // Maximum file size (0 = no limit)
        std::vector<std::string> include_extensions;// Empty = all
        std::vector<std::string> exclude_extensions;
        std::vector<std::string> exclude_patterns;  // Regex patterns to exclude
        bool skip_zero_size = true;                 // Skip empty files
    };

    /**
     * @brief Progress information for duplicate search
     */
    struct DuplicateProgress
    {
        size_t files_scanned = 0;
        size_t total_files = 0;
        size_t duplicates_found = 0;
        uint64_t bytes_scanned = 0;
        uint64_t total_bytes = 0;
        std::string current_file;
        std::string current_phase;      // "Scanning", "Grouping", "Hashing"
        double percentage = 0.0;
    };

    using DuplicateProgressCallback = std::function<void(const DuplicateProgress&)>;
    using DuplicateCompleteCallback = std::function<void(bool success, const std::string& error)>;

    /**
     * @brief Result of duplicate search
     */
    struct DuplicateResult
    {
        std::vector<DuplicateGroup> groups;
        size_t total_files_scanned = 0;
        size_t total_duplicates = 0;
        uint64_t total_wasted_space = 0;
        std::chrono::milliseconds duration{0};
        bool success = false;
        std::string error_message;

        /**
         * @brief Get groups sorted by wasted space (largest first)
         */
        std::vector<const DuplicateGroup*> GetByWastedSpace() const;

        /**
         * @brief Get groups sorted by file count
         */
        std::vector<const DuplicateGroup*> GetByFileCount() const;
    };

    /**
     * @brief Selection mode for auto-selecting files to delete
     */
    enum class AutoSelectMode
    {
        KeepOldest,         // Keep the oldest file, select others
        KeepNewest,         // Keep the newest file, select others
        KeepShortestPath,   // Keep file with shortest path
        KeepLongestPath,    // Keep file with longest path
        KeepInFolder,       // Keep files in specified folder
        KeepNotInFolder     // Keep files NOT in specified folder
    };

    /**
     * @brief Duplicate file finder for Phase 3
     * 
     * Features:
     * - Hash-based duplicate detection
     * - Multiple matching modes
     * - Smart selection algorithms
     * - Background scanning with progress
     * - Duplicate management actions
     */
    class DuplicateFinder
    {
    public:
        DuplicateFinder();
        ~DuplicateFinder();

        // Disable copy
        DuplicateFinder(const DuplicateFinder&) = delete;
        DuplicateFinder& operator=(const DuplicateFinder&) = delete;

        /**
         * @brief Find duplicates in specified paths
         * @param paths Folders to search
         * @param options Search options
         * @param progress_callback Progress callback
         * @return Search result with duplicate groups
         */
        DuplicateResult FindDuplicates(
            const std::vector<core::Path>& paths,
            const DuplicateSearchOptions& options = DuplicateSearchOptions{},
            DuplicateProgressCallback progress_callback = nullptr);

        /**
         * @brief Start async duplicate search
         */
        void FindDuplicatesAsync(
            const std::vector<core::Path>& paths,
            const DuplicateSearchOptions& options,
            DuplicateProgressCallback progress_callback,
            DuplicateCompleteCallback complete_callback);

        /**
         * @brief Cancel ongoing search
         */
        void Cancel();

        /**
         * @brief Check if search is running
         */
        bool IsRunning() const { return running_.load(); }

        /**
         * @brief Get current result (may be partial if running)
         */
        DuplicateResult GetCurrentResult() const;

        /**
         * @brief Auto-select files for deletion based on mode
         * @param groups Groups to process
         * @param mode Selection mode
         * @param folder_path Folder path for KeepInFolder/KeepNotInFolder modes
         * @return Vector of file paths selected for deletion
         */
        std::vector<core::Path> AutoSelect(
            const std::vector<DuplicateGroup>& groups,
            AutoSelectMode mode,
            const core::Path& folder_path = core::Path{});

        /**
         * @brief Delete selected duplicate files
         * @param files Files to delete
         * @param use_recycle_bin Move to recycle bin instead of permanent delete
         * @return Number of files successfully deleted
         */
        size_t DeleteFiles(
            const std::vector<core::Path>& files,
            bool use_recycle_bin = true);

        /**
         * @brief Move selected files to a folder
         * @param files Files to move
         * @param destination Destination folder
         * @return Number of files successfully moved
         */
        size_t MoveFiles(
            const std::vector<core::Path>& files,
            const core::Path& destination);

        /**
         * @brief Create hard links for duplicates (save space while keeping files)
         * @param group Duplicate group to link
         * @param keep_file The file to keep, others become links to it
         * @return true on success
         */
        bool CreateHardLinks(
            const DuplicateGroup& group,
            const core::Path& keep_file);

        /**
         * @brief Export results to CSV
         */
        std::string ExportToCsv(const DuplicateResult& result) const;

        /**
         * @brief Export results to HTML report
         */
        std::string ExportToHtml(const DuplicateResult& result) const;

    private:
        /**
         * @brief Collect files from paths
         */
        std::vector<std::pair<core::Path, uint64_t>> CollectFiles(
            const std::vector<core::Path>& paths,
            const DuplicateSearchOptions& options,
            DuplicateProgressCallback callback);

        /**
         * @brief Group files by size
         */
        std::unordered_map<uint64_t, std::vector<core::Path>> GroupBySize(
            const std::vector<std::pair<core::Path, uint64_t>>& files);

        /**
         * @brief Calculate file hash
         */
        std::string CalculateHash(const core::Path& path, DuplicateMatchMode mode);

        /**
         * @brief Calculate partial hash (first/last blocks)
         */
        std::string CalculatePartialHash(const core::Path& path);

        /**
         * @brief Check if extension matches filter
         */
        bool MatchesExtension(const std::string& ext, 
                              const std::vector<std::string>& include,
                              const std::vector<std::string>& exclude) const;

        /**
         * @brief Check if path matches exclude patterns
         */
        bool MatchesExcludePatterns(const std::string& path,
                                    const std::vector<std::string>& patterns) const;

        std::atomic<bool> running_{false};
        std::atomic<bool> cancel_requested_{false};
        mutable std::mutex result_mutex_;
        DuplicateResult current_result_;
        std::unique_ptr<std::thread> worker_thread_;
    };

} // namespace opacity::batch
