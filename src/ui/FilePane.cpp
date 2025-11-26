#include "opacity/ui/FilePane.h"
#include "opacity/core/Logger.h"

#include <imgui.h>
#include "opacity/ui/ImGuiScoped.h"
#include <algorithm>
#include <cctype>

#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

namespace opacity::ui
{
    uint32_t FilePane::next_id_ = 1;

    FilePane::FilePane(std::shared_ptr<filesystem::FileSystemManager> fs_manager)
        : id_{next_id_++}
        , fs_manager_(std::move(fs_manager))
    {
        SPDLOG_DEBUG("FilePane created with id {}", id_.id);
    }

    FilePane::~FilePane()
    {
        SPDLOG_DEBUG("FilePane {} destroyed", id_.id);
    }

    FilePane::FilePane(FilePane&& other) noexcept
        : id_(other.id_)
        , fs_manager_(std::move(other.fs_manager_))
        , current_path_(std::move(other.current_path_))
        , history_(std::move(other.history_))
        , history_index_(other.history_index_)
        , items_(std::move(other.items_))
        , selection_(std::move(other.selection_))
        , focused_index_(other.focused_index_)
        , file_count_(other.file_count_)
        , directory_count_(other.directory_count_)
        , total_size_(other.total_size_)
        , last_error_(std::move(other.last_error_))
        , sort_column_(other.sort_column_)
        , sort_direction_(other.sort_direction_)
        , show_hidden_(other.show_hidden_)
        , filter_pattern_(std::move(other.filter_pattern_))
        , view_mode_(other.view_mode_)
        , icon_size_(other.icon_size_)
        , custom_title_(std::move(other.custom_title_))
        , on_navigate_(std::move(other.on_navigate_))
        , on_selection_change_(std::move(other.on_selection_change_))
    {
    }

    FilePane& FilePane::operator=(FilePane&& other) noexcept
    {
        if (this != &other)
        {
            id_ = other.id_;
            fs_manager_ = std::move(other.fs_manager_);
            current_path_ = std::move(other.current_path_);
            history_ = std::move(other.history_);
            history_index_ = other.history_index_;
            items_ = std::move(other.items_);
            selection_ = std::move(other.selection_);
            focused_index_ = other.focused_index_;
            file_count_ = other.file_count_;
            directory_count_ = other.directory_count_;
            total_size_ = other.total_size_;
            last_error_ = std::move(other.last_error_);
            sort_column_ = other.sort_column_;
            sort_direction_ = other.sort_direction_;
            show_hidden_ = other.show_hidden_;
            filter_pattern_ = std::move(other.filter_pattern_);
            view_mode_ = other.view_mode_;
            icon_size_ = other.icon_size_;
            custom_title_ = std::move(other.custom_title_);
            on_navigate_ = std::move(other.on_navigate_);
            on_selection_change_ = std::move(other.on_selection_change_);
        }
        return *this;
    }

    std::string FilePane::GetTitle() const
    {
        if (!custom_title_.empty())
            return custom_title_;
        
        if (current_path_.empty())
            return "New Tab";

        // Extract directory name from path
        core::Path path(current_path_);
        std::string name = path.Filename();
        
        if (name.empty())
        {
            // Root directory (e.g., "C:\")
            return current_path_;
        }
        
        return name;
    }

    void FilePane::NavigateTo(const std::string& path)
    {
        NavigateTo(core::Path(path));
    }

    void FilePane::NavigateTo(const core::Path& path)
    {
        std::string normalized = fs_manager_->NormalizePath(path).String();
        
        if (normalized == current_path_)
            return;

        SPDLOG_DEBUG("FilePane {} navigating to: {}", id_.id, normalized);

        // Update history
        if (!current_path_.empty())
        {
            // Remove forward history when navigating to new location
            if (history_index_ + 1 < history_.size())
            {
                history_.erase(history_.begin() + history_index_ + 1, history_.end());
            }
            history_.push_back(normalized);
            history_index_ = history_.size() - 1;
        }
        else
        {
            history_.push_back(normalized);
            history_index_ = 0;
        }

        LoadDirectory(normalized);

        if (on_navigate_)
            on_navigate_(normalized);
    }

    void FilePane::NavigateUp()
    {
        if (!CanNavigateUp())
            return;

        core::Path parent = fs_manager_->GetParentDirectory(core::Path(current_path_));
        if (!parent.String().empty())
        {
            NavigateTo(parent);
        }
    }

    void FilePane::NavigateBack()
    {
        if (!CanNavigateBack())
            return;

        --history_index_;
        LoadDirectory(history_[history_index_]);
        
        if (on_navigate_)
            on_navigate_(current_path_);
    }

    void FilePane::NavigateForward()
    {
        if (!CanNavigateForward())
            return;

        ++history_index_;
        LoadDirectory(history_[history_index_]);
        
        if (on_navigate_)
            on_navigate_(current_path_);
    }

    bool FilePane::CanNavigateBack() const
    {
        return history_index_ > 0;
    }

    bool FilePane::CanNavigateForward() const
    {
        return history_index_ + 1 < history_.size();
    }

    bool FilePane::CanNavigateUp() const
    {
        if (current_path_.empty())
            return false;

        core::Path parent = fs_manager_->GetParentDirectory(core::Path(current_path_));
        return !parent.String().empty() && parent.String() != current_path_;
    }

    void FilePane::Refresh()
    {
        LoadDirectory(current_path_);
    }

    void FilePane::LoadDirectory(const std::string& path)
    {
        current_path_ = path;
        last_error_.clear();

        filesystem::EnumerationOptions options;
        options.include_hidden = show_hidden_;
        options.sort_column = sort_column_;
        options.sort_direction = sort_direction_;
        options.filter_pattern = filter_pattern_;

        auto result = fs_manager_->EnumerateDirectory(core::Path(path), options);

        if (result.success)
        {
            items_ = std::move(result.items);
            file_count_ = result.total_files;
            directory_count_ = result.total_directories;
            total_size_ = result.total_size;
        }
        else
        {
            items_.clear();
            file_count_ = 0;
            directory_count_ = 0;
            total_size_ = 0;
            last_error_ = result.error_message;
            SPDLOG_WARN("Failed to enumerate directory: {}", last_error_);
        }

        // Reset selection
        selection_.assign(items_.size(), false);
        focused_index_ = items_.empty() ? -1 : 0;
    }

    void FilePane::SelectAll()
    {
        std::fill(selection_.begin(), selection_.end(), true);
        if (on_selection_change_)
            on_selection_change_(GetSelectedItems());
    }

    void FilePane::SelectNone()
    {
        std::fill(selection_.begin(), selection_.end(), false);
        if (on_selection_change_)
            on_selection_change_({});
    }

    void FilePane::InvertSelection()
    {
        for (auto& sel : selection_)
            sel = !sel;
        if (on_selection_change_)
            on_selection_change_(GetSelectedItems());
    }

    void FilePane::SetSelection(size_t index, bool selected)
    {
        if (index < selection_.size())
        {
            selection_[index] = selected;
            if (on_selection_change_)
                on_selection_change_(GetSelectedItems());
        }
    }

    void FilePane::ToggleSelection(size_t index)
    {
        if (index < selection_.size())
        {
            selection_[index] = !selection_[index];
            if (on_selection_change_)
                on_selection_change_(GetSelectedItems());
        }
    }

    bool FilePane::IsSelected(size_t index) const
    {
        return index < selection_.size() && selection_[index];
    }

    size_t FilePane::GetSelectionCount() const
    {
        return std::count(selection_.begin(), selection_.end(), true);
    }

    std::vector<filesystem::FsItem> FilePane::GetSelectedItems() const
    {
        std::vector<filesystem::FsItem> result;
        for (size_t i = 0; i < items_.size(); ++i)
        {
            if (IsSelected(i))
                result.push_back(items_[i]);
        }
        return result;
    }

    void FilePane::SetFocusedIndex(int index)
    {
        if (index >= -1 && index < static_cast<int>(items_.size()))
            focused_index_ = index;
    }

    void FilePane::SetSortColumn(filesystem::SortColumn column)
    {
        if (sort_column_ == column)
        {
            ToggleSortDirection();
        }
        else
        {
            sort_column_ = column;
            sort_direction_ = filesystem::SortDirection::Ascending;
        }
        SortItems();
    }

    void FilePane::SetSortDirection(filesystem::SortDirection direction)
    {
        sort_direction_ = direction;
        SortItems();
    }

    void FilePane::ToggleSortDirection()
    {
        sort_direction_ = (sort_direction_ == filesystem::SortDirection::Ascending)
            ? filesystem::SortDirection::Descending
            : filesystem::SortDirection::Ascending;
        SortItems();
    }

    void FilePane::SetShowHidden(bool show)
    {
        if (show_hidden_ != show)
        {
            show_hidden_ = show;
            Refresh();
        }
    }

    void FilePane::SetFilterPattern(const std::string& pattern)
    {
        if (filter_pattern_ != pattern)
        {
            filter_pattern_ = pattern;
            Refresh();
        }
    }

    void FilePane::SortItems()
    {
        // Store selected paths to restore after sort
        std::vector<std::string> selected_paths;
        for (size_t i = 0; i < items_.size(); ++i)
        {
            if (IsSelected(i))
                selected_paths.push_back(items_[i].full_path.String());
        }

        std::string focused_path;
        if (focused_index_ >= 0 && focused_index_ < static_cast<int>(items_.size()))
            focused_path = items_[focused_index_].full_path.String();

        // Re-enumerate with new sort
        Refresh();

        // Restore selection
        for (size_t i = 0; i < items_.size(); ++i)
        {
            const auto& item_path = items_[i].full_path.String();
            if (std::find(selected_paths.begin(), selected_paths.end(), item_path) != selected_paths.end())
                selection_[i] = true;
            if (item_path == focused_path)
                focused_index_ = static_cast<int>(i);
        }
    }

    bool FilePane::Render(float width, float height)
    {
        bool was_interacted = false;

        opacity::ui::ImGuiScopedID pane_id(id_.id);

        // Check for focus
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(0))
        {
            was_interacted = true;
        }

        switch (view_mode_)
        {
        case ViewMode::Details:
            RenderDetailsView();
            break;
        case ViewMode::Icons:
        case ViewMode::Tiles:
        case ViewMode::Thumbnails:
            RenderIconsView();
            break;
        }

        // RAII handles PopID

        return was_interacted;
    }

    void FilePane::RenderDetailsView()
    {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                               ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("##FileList", 4, flags))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Handle sorting
            if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs())
            {
                if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0)
                {
                    const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[0];
                    filesystem::SortColumn new_column;
                    switch (spec.ColumnIndex)
                    {
                    case 0: new_column = filesystem::SortColumn::Name; break;
                    case 1: new_column = filesystem::SortColumn::Size; break;
                    case 2: new_column = filesystem::SortColumn::Type; break;
                    case 3: new_column = filesystem::SortColumn::DateModified; break;
                    default: new_column = filesystem::SortColumn::Name; break;
                    }

                    if (new_column != sort_column_ || 
                        (spec.SortDirection == ImGuiSortDirection_Ascending) != (sort_direction_ == filesystem::SortDirection::Ascending))
                    {
                        sort_column_ = new_column;
                        sort_direction_ = (spec.SortDirection == ImGuiSortDirection_Ascending)
                            ? filesystem::SortDirection::Ascending
                            : filesystem::SortDirection::Descending;
                        SortItems();
                    }
                    sort_specs->SpecsDirty = false;
                }
            }

            // Render items
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(items_.size()));

            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    size_t i = static_cast<size_t>(row);
                    const auto& item = items_[i];

                    ImGui::TableNextRow();

                    // Name column
                    ImGui::TableNextColumn();
                    const char* icon = item.is_directory ? "[DIR] " : "      ";

                    bool is_selected = IsSelected(i);
                    ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns |
                                                     ImGuiSelectableFlags_AllowDoubleClick;

                    std::string label = std::string(icon) + item.name + "##" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), is_selected, sel_flags))
                    {
                        bool ctrl = ImGui::GetIO().KeyCtrl;
                        bool shift = ImGui::GetIO().KeyShift;

                        if (ctrl)
                        {
                            ToggleSelection(i);
                        }
                        else if (shift && focused_index_ >= 0)
                        {
                            SelectNone();
                            size_t start = static_cast<size_t>(focused_index_);
                            size_t end = i;
                            if (start > end) std::swap(start, end);
                            for (size_t j = start; j <= end; ++j)
                                SetSelection(j, true);
                        }
                        else
                        {
                            SelectNone();
                            SetSelection(i, true);
                        }
                        focused_index_ = static_cast<int>(i);

                        // Double-click to open
                        if (ImGui::IsMouseDoubleClicked(0))
                        {
                            HandleItemActivation(i);
                        }
                    }

                    // Size column
                    ImGui::TableNextColumn();
                    if (!item.is_directory)
                    {
                        ImGui::TextUnformatted(item.GetFormattedSize().c_str());
                    }

                    // Type column
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(item.GetTypeDescription().c_str());

                    // Modified column
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(item.GetFormattedModifiedDate().c_str());
                }
            }

            ImGui::EndTable();
        }
    }

    void FilePane::RenderIconsView()
    {
        float icon_size_px = 64.0f;
        switch (icon_size_)
        {
        case 0: icon_size_px = 32.0f; break;
        case 1: icon_size_px = 64.0f; break;
        case 2: icon_size_px = 128.0f; break;
        }

        float item_width = icon_size_px + 16.0f;
        float item_height = icon_size_px + 32.0f;
        float window_width = ImGui::GetContentRegionAvail().x;
        int items_per_row = std::max(1, static_cast<int>(window_width / item_width));

        ImGui::BeginChild("##IconView", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        for (size_t i = 0; i < items_.size(); ++i)
        {
            const auto& item = items_[i];

            if (i % items_per_row != 0)
                ImGui::SameLine();

            // Use RAII helpers to ensure PushID/PopID and BeginGroup/EndGroup pairing
            opacity::ui::ImGuiScopedGroup scoped_group;
            opacity::ui::ImGuiScopedID scoped_id(static_cast<int>(i));

            bool is_selected = IsSelected(i);

            // Draw icon placeholder
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            
            if (is_selected)
            {
                draw_list->AddRectFilled(
                    pos,
                    ImVec2(pos.x + item_width - 8.0f, pos.y + item_height),
                    IM_COL32(100, 149, 237, 100)
                );
            }

            // Icon area (placeholder)
            ImU32 icon_color = item.is_directory ? IM_COL32(255, 200, 100, 255) : IM_COL32(200, 200, 200, 255);
            draw_list->AddRectFilled(
                ImVec2(pos.x + (item_width - icon_size_px) / 2, pos.y),
                ImVec2(pos.x + (item_width + icon_size_px) / 2, pos.y + icon_size_px),
                icon_color
            );

            // Invisible button for selection
            if (ImGui::InvisibleButton("##item", ImVec2(item_width - 8.0f, item_height)))
            {
                bool ctrl = ImGui::GetIO().KeyCtrl;
                if (ctrl)
                {
                    ToggleSelection(i);
                }
                else
                {
                    SelectNone();
                    SetSelection(i, true);
                }
                focused_index_ = static_cast<int>(i);
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                HandleItemActivation(i);
            }

            // Render name (truncated)
            ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + icon_size_px + 2.0f));
            
            std::string display_name = item.name;
            if (display_name.length() > 12)
            {
                display_name = display_name.substr(0, 9) + "...";
            }
            ImGui::TextUnformatted(display_name.c_str());

            // Let RAII destructors pop id and end group
        }

        ImGui::EndChild();
    }

    void FilePane::HandleItemActivation(size_t index)
    {
        if (index >= items_.size())
            return;

        const auto& item = items_[index];

        if (item.is_directory)
        {
            NavigateTo(item.full_path.String());
        }
        else
        {
            // Open with default application
            std::wstring wide_path;
            std::string path_str = item.full_path.String();
            int size_needed = MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(),
                static_cast<int>(path_str.length()), nullptr, 0);
            wide_path.resize(size_needed);
            MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(),
                static_cast<int>(path_str.length()), &wide_path[0], size_needed);
            ShellExecuteW(NULL, L"open", wide_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }

    void FilePane::HandleKeyboardInput()
    {
        ImGuiIO& io = ImGui::GetIO();

        if (!io.WantTextInput)
        {
            // Arrow key navigation
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && focused_index_ > 0)
            {
                if (!io.KeyShift)
                    SelectNone();
                --focused_index_;
                SetSelection(static_cast<size_t>(focused_index_), true);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && focused_index_ < static_cast<int>(items_.size()) - 1)
            {
                if (!io.KeyShift)
                    SelectNone();
                ++focused_index_;
                SetSelection(static_cast<size_t>(focused_index_), true);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Home))
            {
                if (!io.KeyShift)
                    SelectNone();
                focused_index_ = 0;
                if (!items_.empty())
                    SetSelection(0, true);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_End))
            {
                if (!io.KeyShift)
                    SelectNone();
                focused_index_ = static_cast<int>(items_.size()) - 1;
                if (!items_.empty())
                    SetSelection(items_.size() - 1, true);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Enter))
            {
                if (focused_index_ >= 0)
                    HandleItemActivation(static_cast<size_t>(focused_index_));
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
            {
                NavigateUp();
            }

            // Select all
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
            {
                SelectAll();
            }
        }
    }

} // namespace opacity::ui
