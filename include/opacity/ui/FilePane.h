#pragma once

#include "opacity/filesystem/FileSystemManager.h"
#include "opacity/filesystem/FsItem.h"
#include "opacity/core/Path.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace opacity::ui
{
    /**
     * @brief Represents a single file pane that can display directory contents
     * 
     * A FilePane encapsulates:
     * - Current directory and navigation history
     * - Selection state
     * - Sort configuration
     * - View mode settings
     * 
     * Multiple FilePanes can be displayed side-by-side or in tabs.
     */
    class FilePane
    {
    public:
        using NavigationCallback = std::function<void(const std::string& path)>;
        using SelectionCallback = std::function<void(const std::vector<filesystem::FsItem>& items)>;

        /**
         * @brief Unique identifier for this pane
         */
        struct PaneId
        {
            uint32_t id;
            bool operator==(const PaneId& other) const { return id == other.id; }
            bool operator!=(const PaneId& other) const { return id != other.id; }
        };

        FilePane(std::shared_ptr<filesystem::FileSystemManager> fs_manager);
        ~FilePane();

        // Prevent copying
        FilePane(const FilePane&) = delete;
        FilePane& operator=(const FilePane&) = delete;

        // Move support
        FilePane(FilePane&&) noexcept;
        FilePane& operator=(FilePane&&) noexcept;

        /**
         * @brief Get unique pane identifier
         */
        PaneId GetId() const { return id_; }

        /**
         * @brief Set the pane title (shown in tab)
         */
        void SetTitle(const std::string& title) { custom_title_ = title; }

        /**
         * @brief Get the pane title
         */
        std::string GetTitle() const;

        // Navigation
        void NavigateTo(const std::string& path);
        void NavigateTo(const core::Path& path);
        void NavigateUp();
        void NavigateBack();
        void NavigateForward();
        bool CanNavigateBack() const;
        bool CanNavigateForward() const;
        bool CanNavigateUp() const;

        /**
         * @brief Get current directory path
         */
        const std::string& GetCurrentPath() const { return current_path_; }

        /**
         * @brief Refresh the current directory listing
         */
        void Refresh();

        // Selection
        void SelectAll();
        void SelectNone();
        void InvertSelection();
        void SetSelection(size_t index, bool selected);
        void ToggleSelection(size_t index);
        bool IsSelected(size_t index) const;
        size_t GetSelectionCount() const;
        std::vector<filesystem::FsItem> GetSelectedItems() const;

        /**
         * @brief Set the focused item index (for keyboard navigation)
         */
        void SetFocusedIndex(int index);
        int GetFocusedIndex() const { return focused_index_; }

        // Sort & Filter
        void SetSortColumn(filesystem::SortColumn column);
        void SetSortDirection(filesystem::SortDirection direction);
        void ToggleSortDirection();
        filesystem::SortColumn GetSortColumn() const { return sort_column_; }
        filesystem::SortDirection GetSortDirection() const { return sort_direction_; }

        void SetShowHidden(bool show);
        bool GetShowHidden() const { return show_hidden_; }

        void SetFilterPattern(const std::string& pattern);
        const std::string& GetFilterPattern() const { return filter_pattern_; }

        // View Mode
        enum class ViewMode { Details, Icons, Tiles, Thumbnails };
        void SetViewMode(ViewMode mode) { view_mode_ = mode; }
        ViewMode GetViewMode() const { return view_mode_; }

        void SetIconSize(int size) { icon_size_ = size; }
        int GetIconSize() const { return icon_size_; }

        // Content Access
        const std::vector<filesystem::FsItem>& GetItems() const { return items_; }
        size_t GetFileCount() const { return file_count_; }
        size_t GetDirectoryCount() const { return directory_count_; }
        uint64_t GetTotalSize() const { return total_size_; }
        bool HasError() const { return !last_error_.empty(); }
        const std::string& GetLastError() const { return last_error_; }

        // Callbacks
        void SetNavigationCallback(NavigationCallback callback) { on_navigate_ = std::move(callback); }
        void SetSelectionCallback(SelectionCallback callback) { on_selection_change_ = std::move(callback); }

        /**
         * @brief Render the pane contents using ImGui
         * @param width Available width
         * @param height Available height
         * @return true if the pane was interacted with (should gain focus)
         */
        bool Render(float width, float height);

        /**
         * @brief Handle keyboard input when this pane has focus
         */
        void HandleKeyboardInput();

    private:
        void LoadDirectory(const std::string& path);
        void SortItems();
        void RenderDetailsView();
        void RenderIconsView();
        void HandleItemActivation(size_t index);

        static uint32_t next_id_;
        PaneId id_;

        std::shared_ptr<filesystem::FileSystemManager> fs_manager_;

        // Current state
        std::string current_path_;
        std::vector<std::string> history_;
        size_t history_index_ = 0;

        // Content
        std::vector<filesystem::FsItem> items_;
        std::vector<bool> selection_;
        int focused_index_ = -1;
        size_t file_count_ = 0;
        size_t directory_count_ = 0;
        uint64_t total_size_ = 0;
        std::string last_error_;

        // Settings
        filesystem::SortColumn sort_column_ = filesystem::SortColumn::Name;
        filesystem::SortDirection sort_direction_ = filesystem::SortDirection::Ascending;
        bool show_hidden_ = false;
        std::string filter_pattern_;
        ViewMode view_mode_ = ViewMode::Details;
        int icon_size_ = 1; // 0=small, 1=medium, 2=large

        // Custom title (if empty, uses directory name)
        std::string custom_title_;

        // Callbacks
        NavigationCallback on_navigate_;
        SelectionCallback on_selection_change_;
    };

} // namespace opacity::ui
