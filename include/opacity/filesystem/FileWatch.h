#pragma once

#include "opacity/core/Path.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace opacity::filesystem
{
    /**
     * @brief Type of file system change event
     */
    enum class FileChangeType
    {
        Created,
        Modified,
        Deleted,
        Renamed,
        Unknown
    };

    /**
     * @brief Information about a file system change event
     */
    struct FileChangeEvent
    {
        FileChangeType type = FileChangeType::Unknown;
        core::Path path;
        core::Path old_path;  // For rename events
        std::chrono::system_clock::time_point timestamp;
        
        FileChangeEvent() = default;
        FileChangeEvent(FileChangeType t, const core::Path& p)
            : type(t), path(p), timestamp(std::chrono::system_clock::now()) {}
        FileChangeEvent(FileChangeType t, const core::Path& old_p, const core::Path& new_p)
            : type(t), path(new_p), old_path(old_p), timestamp(std::chrono::system_clock::now()) {}
    };

    /**
     * @brief Callback function type for file change notifications
     */
    using FileChangeCallback = std::function<void(const FileChangeEvent&)>;
    using BatchChangeCallback = std::function<void(const std::vector<FileChangeEvent>&)>;

    /**
     * @brief Handle to a watch registration
     */
    using WatchHandle = size_t;

    /**
     * @brief Configuration for file watching
     */
    struct WatchConfig
    {
        bool recursive = true;
        bool watch_files = true;
        bool watch_directories = true;
        bool debounce_events = true;
        std::chrono::milliseconds debounce_delay{100};
        std::vector<std::string> include_patterns;  // Empty = include all
        std::vector<std::string> exclude_patterns;
    };

    /**
     * @brief File system watcher using Windows ReadDirectoryChangesW API
     * 
     * Monitors directories for file system changes and notifies callbacks
     * when files are created, modified, deleted, or renamed.
     */
    class FileWatch
    {
    public:
        FileWatch();
        ~FileWatch();

        // Disable copy
        FileWatch(const FileWatch&) = delete;
        FileWatch& operator=(const FileWatch&) = delete;

        /**
         * @brief Start watching a directory for changes
         * @param path Directory to watch
         * @param callback Function to call when changes occur
         * @param config Watch configuration
         * @return Watch handle, or 0 on failure
         */
        WatchHandle Watch(const core::Path& path, FileChangeCallback callback, 
                         const WatchConfig& config = WatchConfig{});

        /**
         * @brief Start watching with batch notifications
         * @param path Directory to watch
         * @param callback Function to call with batched changes
         * @param config Watch configuration
         * @return Watch handle, or 0 on failure
         */
        WatchHandle WatchBatch(const core::Path& path, BatchChangeCallback callback,
                               const WatchConfig& config = WatchConfig{});

        /**
         * @brief Stop watching a specific path
         * @param handle Watch handle from Watch()
         */
        void Unwatch(WatchHandle handle);

        /**
         * @brief Stop watching all paths
         */
        void UnwatchAll();

        /**
         * @brief Check if a path is being watched
         * @param path Path to check
         * @return true if path is being watched
         */
        bool IsWatching(const core::Path& path) const;

        /**
         * @brief Get the number of active watches
         * @return Number of active watches
         */
        size_t GetWatchCount() const;

        /**
         * @brief Pause all watches
         */
        void Pause();

        /**
         * @brief Resume all watches
         */
        void Resume();

        /**
         * @brief Check if watching is paused
         * @return true if paused
         */
        bool IsPaused() const;

        /**
         * @brief Start the background watcher thread
         */
        void Start();

        /**
         * @brief Stop the background watcher thread
         */
        void Stop();

        /**
         * @brief Check if the watcher is running
         * @return true if running
         */
        bool IsRunning() const;

    private:
        struct WatchEntry;
        
        void WatcherThread();
        void ProcessChanges(WatchEntry& entry);
        bool MatchesFilters(const std::string& filename, const WatchConfig& config) const;
        bool MatchesPattern(const std::string& filename, const std::string& pattern) const;
        void DebounceAndNotify(WatchEntry& entry);

        std::vector<std::unique_ptr<WatchEntry>> watches_;
        mutable std::mutex mutex_;
        std::thread watcher_thread_;
        std::atomic<bool> running_{false};
        std::atomic<bool> paused_{false};
        WatchHandle next_handle_{1};
    };

} // namespace opacity::filesystem
