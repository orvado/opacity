#pragma once

#include "opacity/core/Path.h"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <chrono>

namespace opacity::filesystem
{
    /**
     * @brief Type of file operation
     */
    enum class OperationType
    {
        Copy,
        Move,
        Delete,
        Rename
    };

    /**
     * @brief Status of an operation
     */
    enum class OperationStatus
    {
        Pending,
        InProgress,
        Paused,
        Completed,
        Failed,
        Cancelled
    };

    /**
     * @brief Conflict resolution strategy
     */
    enum class ConflictResolution
    {
        Ask,            // Ask user for each conflict
        Skip,           // Skip conflicting files
        Overwrite,      // Overwrite existing files
        OverwriteOlder, // Overwrite only if source is newer
        Rename,         // Auto-rename (add number suffix)
        KeepBoth        // Keep both files with different names
    };

    /**
     * @brief Describes a conflict during file operations
     */
    struct FileConflict
    {
        core::Path source_path;
        core::Path destination_path;
        uint64_t source_size = 0;
        uint64_t dest_size = 0;
        std::chrono::system_clock::time_point source_modified;
        std::chrono::system_clock::time_point dest_modified;
        bool is_directory = false;
    };

    /**
     * @brief A single file operation item
     */
    struct OperationItem
    {
        core::Path source;
        core::Path destination;
        uint64_t size = 0;
        bool is_directory = false;
    };

    /**
     * @brief Progress information for an operation
     */
    struct OperationProgress
    {
        size_t total_items = 0;
        size_t completed_items = 0;
        uint64_t total_bytes = 0;
        uint64_t completed_bytes = 0;
        std::string current_item;
        double percentage = 0.0;
        double speed_bytes_per_sec = 0.0;
        std::chrono::seconds estimated_remaining{0};
    };

    /**
     * @brief A batch operation containing multiple items
     */
    class BatchOperation
    {
    public:
        using ProgressCallback = std::function<void(const OperationProgress&)>;
        using ConflictCallback = std::function<ConflictResolution(const FileConflict&)>;
        using CompletionCallback = std::function<void(bool success, const std::string& error)>;

        /**
         * @brief Unique operation ID
         */
        struct OperationId
        {
            uint64_t id;
            bool operator==(const OperationId& other) const { return id == other.id; }
        };

        BatchOperation(OperationType type);
        ~BatchOperation();

        // Prevent copying
        BatchOperation(const BatchOperation&) = delete;
        BatchOperation& operator=(const BatchOperation&) = delete;

        /**
         * @brief Get operation ID
         */
        OperationId GetId() const { return id_; }

        /**
         * @brief Get operation type
         */
        OperationType GetType() const { return type_; }

        /**
         * @brief Get operation status
         */
        OperationStatus GetStatus() const { return status_; }

        /**
         * @brief Add an item to the operation
         */
        void AddItem(const OperationItem& item);

        /**
         * @brief Add multiple items
         */
        void AddItems(const std::vector<OperationItem>& items);

        /**
         * @brief Set the destination directory (for copy/move)
         */
        void SetDestination(const core::Path& path) { destination_ = path; }

        /**
         * @brief Get destination directory
         */
        const core::Path& GetDestination() const { return destination_; }

        /**
         * @brief Set default conflict resolution strategy
         */
        void SetConflictResolution(ConflictResolution resolution) { default_resolution_ = resolution; }

        /**
         * @brief Get current progress
         */
        OperationProgress GetProgress() const;

        /**
         * @brief Start the operation
         */
        void Start();

        /**
         * @brief Pause the operation
         */
        void Pause();

        /**
         * @brief Resume a paused operation
         */
        void Resume();

        /**
         * @brief Cancel the operation
         */
        void Cancel();

        /**
         * @brief Wait for operation to complete
         */
        void WaitForCompletion();

        // Callbacks
        void SetProgressCallback(ProgressCallback callback) { on_progress_ = std::move(callback); }
        void SetConflictCallback(ConflictCallback callback) { on_conflict_ = std::move(callback); }
        void SetCompletionCallback(CompletionCallback callback) { on_completion_ = std::move(callback); }

        /**
         * @brief Get list of failed items with error messages
         */
        std::vector<std::pair<std::string, std::string>> GetFailedItems() const;

        /**
         * @brief Get description string for the operation
         */
        std::string GetDescription() const;

    private:
        void ExecuteOperation();
        bool CopyFileInternal(const core::Path& source, const core::Path& dest);
        bool MoveFileInternal(const core::Path& source, const core::Path& dest);
        bool DeleteFileInternal(const core::Path& path);
        ConflictResolution HandleConflict(const FileConflict& conflict);
        core::Path GenerateUniqueName(const core::Path& path);

        static uint64_t next_id_;
        OperationId id_;
        OperationType type_;
        std::atomic<OperationStatus> status_{OperationStatus::Pending};

        std::vector<OperationItem> items_;
        core::Path destination_;
        ConflictResolution default_resolution_ = ConflictResolution::Ask;

        // Progress tracking
        mutable std::mutex progress_mutex_;
        OperationProgress progress_;
        std::chrono::steady_clock::time_point start_time_;
        uint64_t last_progress_bytes_ = 0;
        std::chrono::steady_clock::time_point last_progress_time_;

        // Threading
        std::thread worker_thread_;
        std::atomic<bool> pause_requested_{false};
        std::atomic<bool> cancel_requested_{false};
        std::condition_variable pause_cv_;
        std::mutex pause_mutex_;

        // Failed items
        mutable std::mutex failed_mutex_;
        std::vector<std::pair<std::string, std::string>> failed_items_;

        // Callbacks
        ProgressCallback on_progress_;
        ConflictCallback on_conflict_;
        CompletionCallback on_completion_;
    };

    /**
     * @brief Manages a queue of batch operations
     * 
     * Features:
     * - Operation queue with pause/resume
     * - Conflict resolution
     * - Progress tracking
     * - Concurrent operation limit
     */
    class OperationQueue
    {
    public:
        using QueueChangedCallback = std::function<void()>;

        OperationQueue();
        ~OperationQueue();

        // Prevent copying
        OperationQueue(const OperationQueue&) = delete;
        OperationQueue& operator=(const OperationQueue&) = delete;

        /**
         * @brief Add an operation to the queue
         */
        BatchOperation::OperationId AddOperation(std::unique_ptr<BatchOperation> operation);

        /**
         * @brief Remove a completed/cancelled operation from the queue
         */
        void RemoveOperation(BatchOperation::OperationId id);

        /**
         * @brief Get an operation by ID
         */
        BatchOperation* GetOperation(BatchOperation::OperationId id);
        const BatchOperation* GetOperation(BatchOperation::OperationId id) const;

        /**
         * @brief Get all pending operations
         */
        std::vector<BatchOperation*> GetPendingOperations();

        /**
         * @brief Get all active (in-progress) operations
         */
        std::vector<BatchOperation*> GetActiveOperations();

        /**
         * @brief Get operation count
         */
        size_t GetOperationCount() const;

        /**
         * @brief Get count of active operations
         */
        size_t GetActiveOperationCount() const;

        /**
         * @brief Set maximum concurrent operations
         */
        void SetMaxConcurrent(size_t max) { max_concurrent_ = max; }
        size_t GetMaxConcurrent() const { return max_concurrent_; }

        /**
         * @brief Pause all operations
         */
        void PauseAll();

        /**
         * @brief Resume all operations
         */
        void ResumeAll();

        /**
         * @brief Cancel all operations
         */
        void CancelAll();

        /**
         * @brief Clear completed operations from the queue
         */
        void ClearCompleted();

        /**
         * @brief Process the queue (start pending operations)
         */
        void ProcessQueue();

        /**
         * @brief Set callback for queue changes
         */
        void SetQueueChangedCallback(QueueChangedCallback callback) { on_queue_changed_ = std::move(callback); }

        /**
         * @brief Render queue status UI
         */
        void RenderUI();

    private:
        mutable std::mutex operations_mutex_;
        std::vector<std::unique_ptr<BatchOperation>> operations_;
        size_t max_concurrent_ = 2;

        QueueChangedCallback on_queue_changed_;
    };

} // namespace opacity::filesystem
