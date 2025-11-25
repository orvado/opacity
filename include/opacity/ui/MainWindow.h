#pragma once

#include "opacity/ui/ImGuiBackend.h"
#include "opacity/ui/LayoutManager.h"
#include "opacity/ui/KeybindManager.h"
#include "opacity/ui/Theme.h"
#include "opacity/ui/AdvancedSearchDialog.h"
#include "opacity/ui/DiffViewer.h"
#include "opacity/filesystem/FileSystemManager.h"
#include "opacity/filesystem/FsItem.h"
#include "opacity/filesystem/OperationQueue.h"
#include "opacity/filesystem/FileWatch.h"
#include "opacity/preview/PreviewManager.h"
#include "opacity/search/SearchEngine.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace opacity::ui
{
    /**
     * @brief Main application window for Opacity file manager
     * 
     * Manages the main UI layout including:
     * - Menu bar (File, Edit, View, Help)
     * - Toolbar with common operations  
     * - File list panel
     * - Status bar
     * - Preview panel (optional)
     */
    class MainWindow
    {
    public:
        MainWindow();
        ~MainWindow();

        // Prevent copying
        MainWindow(const MainWindow&) = delete;
        MainWindow& operator=(const MainWindow&) = delete;

        /**
         * @brief Initialize the main window
         * @return true if initialization succeeded
         */
        bool Initialize();

        /**
         * @brief Run the main application loop
         */
        void Run();

        /**
         * @brief Shutdown and cleanup
         */
        void Shutdown();

    private:
        // UI Rendering
        void RenderMenuBar();
        void RenderToolbar();
        void RenderAddressBar();
        void RenderFilePanel();
        void RenderStatusBar();
        void RenderPreviewPanel();
        void RenderDrivesPanel();
        void RenderSearchResults();
        
        // Input handling
        void HandleKeyboardInput();
        
        // Menu handlers
        void HandleFileMenu();
        void HandleEditMenu();
        void HandleViewMenu();
        void HandleHelpMenu();

        // Navigation
        void NavigateTo(const std::string& path);
        void NavigateUp();
        void NavigateBack();
        void NavigateForward();
        void RefreshCurrentDirectory();

        // File operations
        void OpenSelectedItems();
        void CopySelectedItems();
        void CutSelectedItems();
        void PasteItems();
        void DeleteSelectedItems();
        void RenameSelectedItem();
        void CreateNewFolder();

        // Selection
        void SelectAll();
        void InvertSelection();
        void ClearSelection();
        bool IsSelected(size_t index) const;
        void ToggleSelection(size_t index);
        void SetSelection(size_t index, bool selected);

        // Sorting
        void UpdateSort(filesystem::SortColumn column);

        // Search
        void StartSearch();
        void CancelSearch();
        void OnSearchResult(const search::SearchResult& result);

        // Preview
        void UpdatePreview();
        void ReleaseCurrentPreview();

        // Backend
        std::unique_ptr<ImGuiBackend> backend_;

        // Filesystem manager
        std::unique_ptr<filesystem::FileSystemManager> fs_manager_;

        // Preview manager
        std::unique_ptr<preview::PreviewManager> preview_manager_;
        preview::PreviewData current_preview_;
        std::string preview_file_path_;

        // Search engine
        std::unique_ptr<search::SearchEngine> search_engine_;
        std::vector<search::SearchResult> search_results_;
        std::mutex search_results_mutex_;
        size_t search_files_count_ = 0;
        bool show_search_results_ = false;

        // Current state
        std::string current_path_;
        std::vector<std::string> path_history_;
        size_t history_index_ = 0;
        
        // Directory contents cache
        std::vector<filesystem::FsItem> current_items_;
        size_t total_files_ = 0;
        size_t total_dirs_ = 0;
        uint64_t total_size_ = 0;
        std::string last_error_;

        // Selection state
        std::vector<bool> selection_;
        int selected_index_ = -1;  // Last clicked item for preview
        
        // Sort state
        filesystem::SortColumn sort_column_ = filesystem::SortColumn::Name;
        filesystem::SortDirection sort_direction_ = filesystem::SortDirection::Ascending;
        
        // UI state
        bool show_hidden_files_ = false;
        bool show_preview_panel_ = true;
        bool show_drives_panel_ = true;
        int view_mode_ = 0; // 0 = details, 1 = icons
        int icon_size_ = 1; // 0 = small, 1 = medium, 2 = large
        
        // Search/filter
        char search_buffer_[256] = "";
        bool search_active_ = false;

        // Phase 2 components
        std::unique_ptr<LayoutManager> layout_manager_;
        std::unique_ptr<KeybindManager> keybind_manager_;
        std::unique_ptr<Theme> current_theme_;
        std::unique_ptr<AdvancedSearchDialog> advanced_search_dialog_;
        std::unique_ptr<DiffViewer> diff_viewer_;
        std::unique_ptr<filesystem::OperationQueue> operation_queue_;
        std::unique_ptr<filesystem::FileWatch> file_watch_;
        filesystem::WatchHandle current_watch_handle_ = 0;

        // Phase 2 UI state
        bool show_layout_selector_ = false;
        bool show_keybind_editor_ = false;
        bool show_theme_editor_ = false;
        bool show_operation_progress_ = false;

        // Window state
        bool running_ = false;
    };

} // namespace opacity::ui
