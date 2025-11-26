#include "opacity/filesystem/OperationQueue.h"
#include "opacity/core/Logger.h"

#include <imgui.h>
#include "opacity/ui/ImGuiScoped.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace opacity::filesystem
{
    uint64_t BatchOperation::next_id_ = 1;

    BatchOperation::BatchOperation(OperationType type)
        : id_{next_id_++}
        , type_(type)
    {
        SPDLOG_DEBUG("BatchOperation {} created, type: {}", id_.id, static_cast<int>(type));
    }

    BatchOperation::~BatchOperation()
    {
        Cancel();
        if (worker_thread_.joinable())
            worker_thread_.join();
    }

    void BatchOperation::AddItem(const OperationItem& item)
    {
        items_.push_back(item);
        progress_.total_items = items_.size();
        progress_.total_bytes += item.size;
    }

    void BatchOperation::AddItems(const std::vector<OperationItem>& items)
    {
        for (const auto& item : items)
            AddItem(item);
    }

    OperationProgress BatchOperation::GetProgress() const
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        return progress_;
    }

    void BatchOperation::Start()
    {
        if (status_ != OperationStatus::Pending)
            return;

        status_ = OperationStatus::InProgress;
        start_time_ = std::chrono::steady_clock::now();
        last_progress_time_ = start_time_;

        worker_thread_ = std::thread(&BatchOperation::ExecuteOperation, this);
    }

    void BatchOperation::Pause()
    {
        if (status_ == OperationStatus::InProgress)
        {
            pause_requested_ = true;
            status_ = OperationStatus::Paused;
        }
    }

    void BatchOperation::Resume()
    {
        if (status_ == OperationStatus::Paused)
        {
            pause_requested_ = false;
            status_ = OperationStatus::InProgress;
            pause_cv_.notify_all();
        }
    }

    void BatchOperation::Cancel()
    {
        cancel_requested_ = true;
        pause_cv_.notify_all();
        
        if (status_ != OperationStatus::Completed && status_ != OperationStatus::Failed)
            status_ = OperationStatus::Cancelled;
    }

    void BatchOperation::WaitForCompletion()
    {
        if (worker_thread_.joinable())
            worker_thread_.join();
    }

    void BatchOperation::ExecuteOperation()
    {
        SPDLOG_INFO("Starting batch operation {} with {} items", id_.id, items_.size());

        bool success = true;
        std::string error_message;

        for (size_t i = 0; i < items_.size() && !cancel_requested_; ++i)
        {
            // Handle pause
            if (pause_requested_)
            {
                std::unique_lock<std::mutex> lock(pause_mutex_);
                pause_cv_.wait(lock, [this] { return !pause_requested_ || cancel_requested_; });
            }

            if (cancel_requested_)
                break;

            const auto& item = items_[i];
            
            {
                std::lock_guard<std::mutex> lock(progress_mutex_);
                progress_.current_item = item.source.String();
            }

            bool item_success = false;

            switch (type_)
            {
            case OperationType::Copy:
                {
                    core::Path dest = destination_;
                    if (!destination_.String().empty())
                    {
                        dest = core::Path(destination_.String() + "\\" + item.source.Filename());
                    }
                    else
                    {
                        dest = item.destination;
                    }
                    item_success = CopyFileInternal(item.source, dest);
                }
                break;

            case OperationType::Move:
                {
                    core::Path dest = destination_;
                    if (!destination_.String().empty())
                    {
                        dest = core::Path(destination_.String() + "\\" + item.source.Filename());
                    }
                    else
                    {
                        dest = item.destination;
                    }
                    item_success = MoveFileInternal(item.source, dest);
                }
                break;

            case OperationType::Delete:
                item_success = DeleteFileInternal(item.source);
                break;

            case OperationType::Rename:
                item_success = MoveFileInternal(item.source, item.destination);
                break;
            }

            if (!item_success)
            {
                success = false;
            }

            // Update progress
            {
                std::lock_guard<std::mutex> lock(progress_mutex_);
                progress_.completed_items = i + 1;
                progress_.completed_bytes += item.size;
                progress_.percentage = (progress_.total_bytes > 0) 
                    ? (100.0 * progress_.completed_bytes / progress_.total_bytes) 
                    : (100.0 * progress_.completed_items / progress_.total_items);

                // Calculate speed
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_progress_time_);
                if (elapsed.count() > 100)
                {
                    uint64_t bytes_diff = progress_.completed_bytes - last_progress_bytes_;
                    progress_.speed_bytes_per_sec = (bytes_diff * 1000.0) / elapsed.count();
                    last_progress_bytes_ = progress_.completed_bytes;
                    last_progress_time_ = now;

                    // Estimate remaining time
                    if (progress_.speed_bytes_per_sec > 0)
                    {
                        uint64_t remaining_bytes = progress_.total_bytes - progress_.completed_bytes;
                        auto remaining_sec = static_cast<int64_t>(remaining_bytes / progress_.speed_bytes_per_sec);
                        progress_.estimated_remaining = std::chrono::seconds(remaining_sec);
                    }
                }
            }

            if (on_progress_)
                on_progress_(GetProgress());
        }

        if (cancel_requested_)
        {
            status_ = OperationStatus::Cancelled;
            SPDLOG_INFO("Batch operation {} cancelled", id_.id);
        }
        else if (!success)
        {
            status_ = OperationStatus::Failed;
            error_message = "Some items failed to process";
            SPDLOG_WARN("Batch operation {} completed with errors", id_.id);
        }
        else
        {
            status_ = OperationStatus::Completed;
            SPDLOG_INFO("Batch operation {} completed successfully", id_.id);
        }

        if (on_completion_)
            on_completion_(success, error_message);
    }

    bool BatchOperation::CopyFileInternal(const core::Path& source, const core::Path& dest)
    {
        try
        {
            fs::path src_path(source.String());
            fs::path dst_path(dest.String());

            // Check for conflict
            if (fs::exists(dst_path))
            {
                FileConflict conflict;
                conflict.source_path = source;
                conflict.destination_path = dest;
                conflict.is_directory = fs::is_directory(dst_path);

                if (!conflict.is_directory)
                {
                    conflict.source_size = fs::file_size(src_path);
                    conflict.dest_size = fs::file_size(dst_path);
                }

                ConflictResolution resolution = HandleConflict(conflict);

                switch (resolution)
                {
                case ConflictResolution::Skip:
                    return true;
                case ConflictResolution::Overwrite:
                    fs::remove_all(dst_path);
                    break;
                case ConflictResolution::Rename:
                    dst_path = fs::path(GenerateUniqueName(dest).String());
                    break;
                default:
                    break;
                }
            }

            // Create parent directories
            fs::create_directories(dst_path.parent_path());

            // Copy
            if (fs::is_directory(src_path))
            {
                fs::copy(src_path, dst_path, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }
            else
            {
                fs::copy_file(src_path, dst_path, fs::copy_options::overwrite_existing);
            }

            return true;
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(failed_mutex_);
            failed_items_.emplace_back(source.String(), e.what());
            SPDLOG_ERROR("Failed to copy {}: {}", source.String(), e.what());
            return false;
        }
    }

    bool BatchOperation::MoveFileInternal(const core::Path& source, const core::Path& dest)
    {
        try
        {
            fs::path src_path(source.String());
            fs::path dst_path(dest.String());

            // Check for conflict
            if (fs::exists(dst_path))
            {
                FileConflict conflict;
                conflict.source_path = source;
                conflict.destination_path = dest;

                ConflictResolution resolution = HandleConflict(conflict);

                switch (resolution)
                {
                case ConflictResolution::Skip:
                    return true;
                case ConflictResolution::Overwrite:
                    fs::remove_all(dst_path);
                    break;
                case ConflictResolution::Rename:
                    dst_path = fs::path(GenerateUniqueName(dest).String());
                    break;
                default:
                    break;
                }
            }

            // Create parent directories
            fs::create_directories(dst_path.parent_path());

            // Move
            fs::rename(src_path, dst_path);

            return true;
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(failed_mutex_);
            failed_items_.emplace_back(source.String(), e.what());
            SPDLOG_ERROR("Failed to move {}: {}", source.String(), e.what());
            return false;
        }
    }

    bool BatchOperation::DeleteFileInternal(const core::Path& path)
    {
        try
        {
            fs::path fspath(path.String());
            
            if (fs::is_directory(fspath))
            {
                fs::remove_all(fspath);
            }
            else
            {
                fs::remove(fspath);
            }

            return true;
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(failed_mutex_);
            failed_items_.emplace_back(path.String(), e.what());
            SPDLOG_ERROR("Failed to delete {}: {}", path.String(), e.what());
            return false;
        }
    }

    ConflictResolution BatchOperation::HandleConflict(const FileConflict& conflict)
    {
        if (on_conflict_)
            return on_conflict_(conflict);
        return default_resolution_;
    }

    core::Path BatchOperation::GenerateUniqueName(const core::Path& path)
    {
        fs::path fspath(path.String());
        fs::path parent = fspath.parent_path();
        fs::path stem = fspath.stem();
        fs::path ext = fspath.extension();

        int counter = 1;
        fs::path new_path;

        do
        {
            std::string new_name = stem.string() + " (" + std::to_string(counter) + ")" + ext.string();
            new_path = parent / new_name;
            ++counter;
        } while (fs::exists(new_path) && counter < 1000);

        return core::Path(new_path.string());
    }

    std::vector<std::pair<std::string, std::string>> BatchOperation::GetFailedItems() const
    {
        std::lock_guard<std::mutex> lock(failed_mutex_);
        return failed_items_;
    }

    std::string BatchOperation::GetDescription() const
    {
        std::string type_str;
        switch (type_)
        {
        case OperationType::Copy: type_str = "Copying"; break;
        case OperationType::Move: type_str = "Moving"; break;
        case OperationType::Delete: type_str = "Deleting"; break;
        case OperationType::Rename: type_str = "Renaming"; break;
        }

        return type_str + " " + std::to_string(items_.size()) + " items";
    }

    // OperationQueue implementation
    OperationQueue::OperationQueue() = default;

    OperationQueue::~OperationQueue()
    {
        CancelAll();
    }

    BatchOperation::OperationId OperationQueue::AddOperation(std::unique_ptr<BatchOperation> operation)
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        auto id = operation->GetId();
        operations_.push_back(std::move(operation));
        
        SPDLOG_INFO("Operation {} added to queue", id.id);
        
        if (on_queue_changed_)
            on_queue_changed_();
        
        return id;
    }

    void OperationQueue::RemoveOperation(BatchOperation::OperationId id)
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        auto it = std::find_if(operations_.begin(), operations_.end(),
            [id](const auto& op) { return op->GetId() == id; });
        
        if (it != operations_.end())
        {
            operations_.erase(it);
            
            if (on_queue_changed_)
                on_queue_changed_();
        }
    }

    BatchOperation* OperationQueue::GetOperation(BatchOperation::OperationId id)
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        auto it = std::find_if(operations_.begin(), operations_.end(),
            [id](const auto& op) { return op->GetId() == id; });
        
        return (it != operations_.end()) ? it->get() : nullptr;
    }

    const BatchOperation* OperationQueue::GetOperation(BatchOperation::OperationId id) const
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        auto it = std::find_if(operations_.begin(), operations_.end(),
            [id](const auto& op) { return op->GetId() == id; });
        
        return (it != operations_.end()) ? it->get() : nullptr;
    }

    std::vector<BatchOperation*> OperationQueue::GetPendingOperations()
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        std::vector<BatchOperation*> result;
        
        for (auto& op : operations_)
        {
            if (op->GetStatus() == OperationStatus::Pending)
                result.push_back(op.get());
        }
        
        return result;
    }

    std::vector<BatchOperation*> OperationQueue::GetActiveOperations()
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        std::vector<BatchOperation*> result;
        
        for (auto& op : operations_)
        {
            if (op->GetStatus() == OperationStatus::InProgress)
                result.push_back(op.get());
        }
        
        return result;
    }

    size_t OperationQueue::GetOperationCount() const
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        return operations_.size();
    }

    size_t OperationQueue::GetActiveOperationCount() const
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        size_t count = 0;
        
        for (const auto& op : operations_)
        {
            if (op->GetStatus() == OperationStatus::InProgress)
                ++count;
        }
        
        return count;
    }

    void OperationQueue::PauseAll()
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        for (auto& op : operations_)
        {
            if (op->GetStatus() == OperationStatus::InProgress)
                op->Pause();
        }
    }

    void OperationQueue::ResumeAll()
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        for (auto& op : operations_)
        {
            if (op->GetStatus() == OperationStatus::Paused)
                op->Resume();
        }
    }

    void OperationQueue::CancelAll()
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        for (auto& op : operations_)
        {
            op->Cancel();
        }
    }

    void OperationQueue::ClearCompleted()
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        operations_.erase(
            std::remove_if(operations_.begin(), operations_.end(),
                [](const auto& op) {
                    auto status = op->GetStatus();
                    return status == OperationStatus::Completed ||
                           status == OperationStatus::Cancelled ||
                           status == OperationStatus::Failed;
                }),
            operations_.end()
        );
        
        if (on_queue_changed_)
            on_queue_changed_();
    }

    void OperationQueue::ProcessQueue()
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        
        size_t active = 0;
        for (const auto& op : operations_)
        {
            if (op->GetStatus() == OperationStatus::InProgress)
                ++active;
        }

        // Start pending operations if we have capacity
        for (auto& op : operations_)
        {
            if (active >= max_concurrent_)
                break;
            
            if (op->GetStatus() == OperationStatus::Pending)
            {
                op->Start();
                ++active;
            }
        }
    }

    void OperationQueue::RenderUI()
    {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        
        if (operations_.empty())
        {
            ImGui::TextDisabled("No active operations");
            return;
        }

        for (size_t i = 0; i < operations_.size(); ++i)
        {
            auto& op = operations_[i];
            auto progress = op->GetProgress();
            auto status = op->GetStatus();

            opacity::ui::ImGuiScopedID scoped_id(static_cast<int>(i));

            // Status color
            ImVec4 status_color;
            const char* status_text;
            switch (status)
            {
            case OperationStatus::Pending:
                status_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                status_text = "Pending";
                break;
            case OperationStatus::InProgress:
                status_color = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
                status_text = "In Progress";
                break;
            case OperationStatus::Paused:
                status_color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                status_text = "Paused";
                break;
            case OperationStatus::Completed:
                status_color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
                status_text = "Completed";
                break;
            case OperationStatus::Failed:
                status_color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                status_text = "Failed";
                break;
            case OperationStatus::Cancelled:
                status_color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                status_text = "Cancelled";
                break;
            default:
                status_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                status_text = "Unknown";
            }

            ImGui::TextColored(status_color, "[%s]", status_text);
            ImGui::SameLine();
            ImGui::Text("%s", op->GetDescription().c_str());

            // Progress bar
            if (status == OperationStatus::InProgress || status == OperationStatus::Paused)
            {
                ImGui::ProgressBar(static_cast<float>(progress.percentage / 100.0), ImVec2(-1, 0));
                
                // Speed and ETA
                if (progress.speed_bytes_per_sec > 0)
                {
                    double speed_mb = progress.speed_bytes_per_sec / (1024.0 * 1024.0);
                    auto eta = progress.estimated_remaining.count();
                    ImGui::Text("%.1f MB/s - %lld:%02lld remaining", 
                        speed_mb, eta / 60, eta % 60);
                }

                // Control buttons
                if (status == OperationStatus::InProgress)
                {
                    if (ImGui::SmallButton("Pause"))
                        op->Pause();
                }
                else if (status == OperationStatus::Paused)
                {
                    if (ImGui::SmallButton("Resume"))
                        op->Resume();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Cancel"))
                    op->Cancel();
            }

            ImGui::Separator();
            // RAII will pop ID
        }

        // Queue controls
        if (ImGui::Button("Pause All"))
            PauseAll();
        ImGui::SameLine();
        if (ImGui::Button("Resume All"))
            ResumeAll();
        ImGui::SameLine();
        if (ImGui::Button("Clear Completed"))
            ClearCompleted();
    }

} // namespace opacity::filesystem
