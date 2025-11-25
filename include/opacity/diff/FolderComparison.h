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

namespace opacity::diff
{
    /**
     * @brief How to compare files during folder comparison
     */
    enum class ComparisonMode
    {
        Name,           // Compare by name only
        Size,           // Compare by name and size
        Date,           // Compare by name and modification date
        Hash,           // Compare by name and content hash (MD5/SHA256)
        Content         // Full byte-by-byte comparison
    };

    /**
     * @brief Status of a file in comparison results
     */
    enum class ComparisonStatus
    {
        Identical,      // File exists in both and matches
        LeftOnly,       // File exists only in left folder
        RightOnly,      // File exists only in right folder
        Different,      // File exists in both but differs
        Error           // Error comparing this file
    };

    /**
     * @brief Direction for sync operations
     */
    enum class SyncDirection
    {
        LeftToRight,    // Copy/update from left to right
        RightToLeft,    // Copy/update from right to left
        Bidirectional,  // Sync both ways (latest wins)
        Mirror          // Make right identical to left
    };

    /**
     * @brief Result for a single compared item
     */
    struct ComparisonItem
    {
        std::string relative_path;          // Path relative to comparison root
        ComparisonStatus status = ComparisonStatus::Identical;
        
        // Left side info
        bool left_exists = false;
        uint64_t left_size = 0;
        std::chrono::system_clock::time_point left_modified;
        bool left_is_directory = false;
        std::string left_hash;
        
        // Right side info
        bool right_exists = false;
        uint64_t right_size = 0;
        std::chrono::system_clock::time_point right_modified;
        bool right_is_directory = false;
        std::string right_hash;
        
        std::string error_message;

        /**
         * @brief Get the newer side for this item
         * @return true if left is newer, false if right is newer
         */
        bool IsLeftNewer() const;
    };

    /**
     * @brief Statistics for folder comparison
     */
    struct ComparisonStats
    {
        size_t total_items = 0;
        size_t identical_files = 0;
        size_t different_files = 0;
        size_t left_only_files = 0;
        size_t right_only_files = 0;
        size_t identical_dirs = 0;
        size_t left_only_dirs = 0;
        size_t right_only_dirs = 0;
        size_t errors = 0;
        
        uint64_t left_total_size = 0;
        uint64_t right_total_size = 0;
        uint64_t different_size = 0;
        
        std::chrono::milliseconds duration{0};
    };

    /**
     * @brief Options for folder comparison
     */
    struct FolderComparisonOptions
    {
        ComparisonMode mode = ComparisonMode::Size;
        bool recursive = true;
        bool include_hidden = false;
        bool ignore_case = true;                // Case-insensitive name matching
        bool compare_timestamps = false;        // Also check modification times
        std::vector<std::string> exclude_patterns;  // Patterns to exclude
        std::vector<std::string> include_patterns;  // Patterns to include (empty = all)
        size_t max_depth = 0;                   // 0 = unlimited
    };

    /**
     * @brief Progress callback data
     */
    struct ComparisonProgress
    {
        size_t files_processed = 0;
        size_t total_files = 0;
        std::string current_file;
        double percentage = 0.0;
        bool can_cancel = true;
    };

    using ComparisonProgressCallback = std::function<void(const ComparisonProgress&)>;
    using ComparisonCompleteCallback = std::function<void(bool success, const std::string& error)>;

    /**
     * @brief Full result of a folder comparison
     */
    struct FolderComparisonResult
    {
        core::Path left_root;
        core::Path right_root;
        FolderComparisonOptions options;
        
        std::vector<ComparisonItem> items;
        ComparisonStats stats;
        
        bool success = false;
        std::string error_message;
        
        /**
         * @brief Get items filtered by status
         */
        std::vector<const ComparisonItem*> GetByStatus(ComparisonStatus status) const;
        
        /**
         * @brief Get only different items (not identical)
         */
        std::vector<const ComparisonItem*> GetDifferences() const;
        
        /**
         * @brief Check if folders are identical
         */
        bool AreIdentical() const;
    };

    /**
     * @brief Sync operation result
     */
    struct SyncResult
    {
        size_t files_copied = 0;
        size_t files_deleted = 0;
        size_t files_updated = 0;
        size_t errors = 0;
        std::vector<std::string> error_messages;
        bool success = false;
    };

    /**
     * @brief Folder comparison engine for Phase 3
     * 
     * Features:
     * - Recursive folder comparison
     * - Multiple comparison modes (name, size, hash, content)
     * - Visual diff result presentation
     * - Sync operations from comparison view
     * - Background comparison with progress
     * - Cancellation support
     */
    class FolderComparison
    {
    public:
        FolderComparison();
        ~FolderComparison();

        // Disable copy
        FolderComparison(const FolderComparison&) = delete;
        FolderComparison& operator=(const FolderComparison&) = delete;

        /**
         * @brief Compare two folders synchronously
         * @param left_path Left/source folder
         * @param right_path Right/destination folder
         * @param options Comparison options
         * @param progress_callback Optional progress callback
         * @return Comparison results
         */
        FolderComparisonResult Compare(
            const core::Path& left_path,
            const core::Path& right_path,
            const FolderComparisonOptions& options = FolderComparisonOptions{},
            ComparisonProgressCallback progress_callback = nullptr);

        /**
         * @brief Start async folder comparison
         * @param left_path Left/source folder
         * @param right_path Right/destination folder
         * @param options Comparison options
         * @param progress_callback Progress callback
         * @param complete_callback Completion callback
         */
        void CompareAsync(
            const core::Path& left_path,
            const core::Path& right_path,
            const FolderComparisonOptions& options,
            ComparisonProgressCallback progress_callback,
            ComparisonCompleteCallback complete_callback);

        /**
         * @brief Cancel ongoing comparison
         */
        void Cancel();

        /**
         * @brief Check if comparison is in progress
         */
        bool IsRunning() const { return running_.load(); }

        /**
         * @brief Get current comparison result (may be partial if running)
         */
        FolderComparisonResult GetCurrentResult() const;

        /**
         * @brief Sync folders based on comparison results
         * @param result Previous comparison result
         * @param direction Sync direction
         * @param selected_items Optional list of items to sync (nullptr = all)
         * @return Sync operation result
         */
        SyncResult SyncFolders(
            const FolderComparisonResult& result,
            SyncDirection direction,
            const std::vector<size_t>* selected_items = nullptr);

        /**
         * @brief Export comparison results to various formats
         */
        std::string ExportToCsv(const FolderComparisonResult& result) const;
        std::string ExportToHtml(const FolderComparisonResult& result) const;
        std::string ExportToText(const FolderComparisonResult& result) const;

    private:
        /**
         * @brief Build file list for a folder
         */
        std::vector<std::pair<std::string, filesystem::FsItem>> EnumerateFolder(
            const core::Path& root,
            const FolderComparisonOptions& options,
            size_t current_depth = 0);

        /**
         * @brief Compare two files based on mode
         */
        ComparisonStatus CompareFiles(
            const core::Path& left_path,
            const core::Path& right_path,
            ComparisonMode mode,
            ComparisonItem& item);

        /**
         * @brief Calculate file hash for comparison
         */
        std::string CalculateHash(const core::Path& path) const;

        /**
         * @brief Check if path matches patterns
         */
        bool MatchesPatterns(
            const std::string& path,
            const std::vector<std::string>& include,
            const std::vector<std::string>& exclude) const;

        // State
        std::atomic<bool> running_{false};
        std::atomic<bool> cancel_requested_{false};
        mutable std::mutex result_mutex_;
        FolderComparisonResult current_result_;
        std::unique_ptr<std::thread> worker_thread_;
    };

} // namespace opacity::diff
