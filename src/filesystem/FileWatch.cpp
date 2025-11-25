#include "opacity/filesystem/FileWatch.h"
#include "opacity/core/Logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <regex>

namespace opacity::filesystem
{
    struct FileWatch::WatchEntry
    {
        WatchHandle handle = 0;
        core::Path path;
        WatchConfig config;
        FileChangeCallback callback;
        BatchChangeCallback batch_callback;
        bool is_batch = false;
        
        HANDLE dir_handle = INVALID_HANDLE_VALUE;
        OVERLAPPED overlapped{};
        std::vector<BYTE> buffer;
        
        std::vector<FileChangeEvent> pending_events;
        std::chrono::steady_clock::time_point last_event_time;
        std::mutex event_mutex;
        
        WatchEntry()
        {
            buffer.resize(64 * 1024);  // 64KB buffer
            overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        }
        
        ~WatchEntry()
        {
            if (dir_handle != INVALID_HANDLE_VALUE)
            {
                CancelIo(dir_handle);
                CloseHandle(dir_handle);
            }
            if (overlapped.hEvent != nullptr)
            {
                CloseHandle(overlapped.hEvent);
            }
        }
    };

    FileWatch::FileWatch() = default;

    FileWatch::~FileWatch()
    {
        Stop();
        UnwatchAll();
    }

    WatchHandle FileWatch::Watch(const core::Path& path, FileChangeCallback callback,
                                  const WatchConfig& config)
    {
        auto entry = std::make_unique<WatchEntry>();
        entry->handle = next_handle_++;
        entry->path = path;
        entry->config = config;
        entry->callback = std::move(callback);
        entry->is_batch = false;

        // Open directory handle
        DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        DWORD flags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED;
        
        entry->dir_handle = CreateFileW(
            path.WString().c_str(),
            FILE_LIST_DIRECTORY,
            share_mode,
            nullptr,
            OPEN_EXISTING,
            flags,
            nullptr
        );

        if (entry->dir_handle == INVALID_HANDLE_VALUE)
        {
            SPDLOG_ERROR("Failed to open directory for watching: {}", path.String());
            return 0;
        }

        WatchHandle handle = entry->handle;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            watches_.push_back(std::move(entry));
        }

        SPDLOG_INFO("Started watching: {}", path.String());
        return handle;
    }

    WatchHandle FileWatch::WatchBatch(const core::Path& path, BatchChangeCallback callback,
                                       const WatchConfig& config)
    {
        auto entry = std::make_unique<WatchEntry>();
        entry->handle = next_handle_++;
        entry->path = path;
        entry->config = config;
        entry->batch_callback = std::move(callback);
        entry->is_batch = true;

        DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        DWORD flags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED;
        
        entry->dir_handle = CreateFileW(
            path.WString().c_str(),
            FILE_LIST_DIRECTORY,
            share_mode,
            nullptr,
            OPEN_EXISTING,
            flags,
            nullptr
        );

        if (entry->dir_handle == INVALID_HANDLE_VALUE)
        {
            SPDLOG_ERROR("Failed to open directory for watching: {}", path.String());
            return 0;
        }

        WatchHandle handle = entry->handle;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            watches_.push_back(std::move(entry));
        }

        SPDLOG_INFO("Started batch watching: {}", path.String());
        return handle;
    }

    void FileWatch::Unwatch(WatchHandle handle)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = std::find_if(watches_.begin(), watches_.end(),
            [handle](const auto& entry) { return entry->handle == handle; });
        
        if (it != watches_.end())
        {
            SPDLOG_INFO("Stopped watching: {}", (*it)->path.String());
            watches_.erase(it);
        }
    }

    void FileWatch::UnwatchAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watches_.clear();
        SPDLOG_INFO("Stopped all watches");
    }

    bool FileWatch::IsWatching(const core::Path& path) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        return std::any_of(watches_.begin(), watches_.end(),
            [&path](const auto& entry) { return entry->path == path; });
    }

    size_t FileWatch::GetWatchCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return watches_.size();
    }

    void FileWatch::Pause()
    {
        paused_ = true;
    }

    void FileWatch::Resume()
    {
        paused_ = false;
    }

    bool FileWatch::IsPaused() const
    {
        return paused_;
    }

    void FileWatch::Start()
    {
        if (running_)
            return;

        running_ = true;
        watcher_thread_ = std::thread(&FileWatch::WatcherThread, this);
        SPDLOG_INFO("FileWatch thread started");
    }

    void FileWatch::Stop()
    {
        if (!running_)
            return;

        running_ = false;
        
        // Signal all overlapped events to wake up the thread
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& entry : watches_)
            {
                if (entry->overlapped.hEvent)
                {
                    SetEvent(entry->overlapped.hEvent);
                }
            }
        }

        if (watcher_thread_.joinable())
        {
            watcher_thread_.join();
        }

        SPDLOG_INFO("FileWatch thread stopped");
    }

    bool FileWatch::IsRunning() const
    {
        return running_;
    }

    void FileWatch::WatcherThread()
    {
        while (running_)
        {
            if (paused_)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            std::vector<WatchEntry*> entries;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& entry : watches_)
                {
                    entries.push_back(entry.get());
                }
            }

            if (entries.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // Issue read requests for each watch
            for (auto* entry : entries)
            {
                DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                               FILE_NOTIFY_CHANGE_DIR_NAME |
                               FILE_NOTIFY_CHANGE_ATTRIBUTES |
                               FILE_NOTIFY_CHANGE_SIZE |
                               FILE_NOTIFY_CHANGE_LAST_WRITE |
                               FILE_NOTIFY_CHANGE_CREATION;

                BOOL success = ReadDirectoryChangesW(
                    entry->dir_handle,
                    entry->buffer.data(),
                    static_cast<DWORD>(entry->buffer.size()),
                    entry->config.recursive ? TRUE : FALSE,
                    filter,
                    nullptr,
                    &entry->overlapped,
                    nullptr
                );

                if (!success)
                {
                    SPDLOG_ERROR("ReadDirectoryChangesW failed for: {}", entry->path.String());
                }
            }

            // Wait for any changes
            std::vector<HANDLE> events;
            for (auto* entry : entries)
            {
                if (entry->overlapped.hEvent)
                {
                    events.push_back(entry->overlapped.hEvent);
                }
            }

            if (events.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            DWORD wait_result = WaitForMultipleObjects(
                static_cast<DWORD>(events.size()),
                events.data(),
                FALSE,
                100  // 100ms timeout for responsive shutdown
            );

            if (!running_)
                break;

            if (wait_result == WAIT_TIMEOUT)
            {
                // Check for debounced events
                for (auto* entry : entries)
                {
                    DebounceAndNotify(*entry);
                }
                continue;
            }

            if (wait_result >= WAIT_OBJECT_0 && 
                wait_result < WAIT_OBJECT_0 + events.size())
            {
                size_t index = wait_result - WAIT_OBJECT_0;
                if (index < entries.size())
                {
                    ProcessChanges(*entries[index]);
                    ResetEvent(entries[index]->overlapped.hEvent);
                }
            }
        }
    }

    void FileWatch::ProcessChanges(WatchEntry& entry)
    {
        DWORD bytes_returned = 0;
        if (!GetOverlappedResult(entry.dir_handle, &entry.overlapped, &bytes_returned, FALSE))
        {
            return;
        }

        if (bytes_returned == 0)
            return;

        FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(entry.buffer.data());

        while (true)
        {
            // Get the filename
            std::wstring filename(info->FileName, info->FileNameLength / sizeof(wchar_t));
            core::Path full_path = entry.path / core::Path(filename);

            // Determine change type
            FileChangeType change_type = FileChangeType::Unknown;
            switch (info->Action)
            {
            case FILE_ACTION_ADDED:
                change_type = FileChangeType::Created;
                break;
            case FILE_ACTION_REMOVED:
                change_type = FileChangeType::Deleted;
                break;
            case FILE_ACTION_MODIFIED:
                change_type = FileChangeType::Modified;
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
            case FILE_ACTION_RENAMED_NEW_NAME:
                change_type = FileChangeType::Renamed;
                break;
            }

            // Check if this file matches our filters
            if (MatchesFilters(full_path.Filename(), entry.config))
            {
                FileChangeEvent event(change_type, full_path);

                if (entry.is_batch || entry.config.debounce_events)
                {
                    std::lock_guard<std::mutex> lock(entry.event_mutex);
                    entry.pending_events.push_back(event);
                    entry.last_event_time = std::chrono::steady_clock::now();
                }
                else if (entry.callback)
                {
                    entry.callback(event);
                }
            }

            // Move to next notification
            if (info->NextEntryOffset == 0)
                break;

            info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<BYTE*>(info) + info->NextEntryOffset);
        }
    }

    bool FileWatch::MatchesFilters(const std::string& filename, const WatchConfig& config) const
    {
        // Check exclude patterns first
        for (const auto& pattern : config.exclude_patterns)
        {
            if (MatchesPattern(filename, pattern))
                return false;
        }

        // If no include patterns, include everything
        if (config.include_patterns.empty())
            return true;

        // Check include patterns
        for (const auto& pattern : config.include_patterns)
        {
            if (MatchesPattern(filename, pattern))
                return true;
        }

        return false;
    }

    bool FileWatch::MatchesPattern(const std::string& filename, const std::string& pattern) const
    {
        // Simple glob-style matching
        // Convert glob to regex
        std::string regex_pattern;
        for (char c : pattern)
        {
            switch (c)
            {
            case '*':
                regex_pattern += ".*";
                break;
            case '?':
                regex_pattern += ".";
                break;
            case '.':
            case '[':
            case ']':
            case '(':
            case ')':
            case '+':
            case '^':
            case '$':
            case '|':
            case '\\':
                regex_pattern += '\\';
                regex_pattern += c;
                break;
            default:
                regex_pattern += c;
                break;
            }
        }

        try
        {
            std::regex re(regex_pattern, std::regex::icase);
            return std::regex_match(filename, re);
        }
        catch (const std::regex_error&)
        {
            return false;
        }
    }

    void FileWatch::DebounceAndNotify(WatchEntry& entry)
    {
        std::lock_guard<std::mutex> lock(entry.event_mutex);

        if (entry.pending_events.empty())
            return;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - entry.last_event_time);

        if (elapsed < entry.config.debounce_delay)
            return;

        // Time to notify
        std::vector<FileChangeEvent> events = std::move(entry.pending_events);
        entry.pending_events.clear();

        if (entry.is_batch && entry.batch_callback)
        {
            entry.batch_callback(events);
        }
        else if (entry.callback)
        {
            for (const auto& event : events)
            {
                entry.callback(event);
            }
        }
    }

} // namespace opacity::filesystem
