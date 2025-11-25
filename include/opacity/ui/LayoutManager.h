#pragma once

#include "opacity/ui/TabManager.h"
#include <memory>
#include <array>
#include <functional>

namespace opacity::ui
{
    /**
     * @brief Layout types for pane arrangement
     */
    enum class LayoutType
    {
        Single,           // Single pane
        DualHorizontal,   // Two panes side by side
        DualVertical,     // Two panes stacked
        TripleLeft,       // One large pane left, two stacked right
        TripleRight,      // Two stacked left, one large pane right
        Quad              // 2x2 grid
    };

    /**
     * @brief Manages the layout of multiple panes in the main window
     * 
     * Features:
     * - Multiple layout configurations (single, dual, triple, quad)
     * - Resizable splits
     * - Pane focus management
     * - Pane synchronization options
     */
    class LayoutManager
    {
    public:
        static constexpr size_t MAX_PANES = 4;

        using PaneId = FilePane::PaneId;
        using FocusChangedCallback = std::function<void(size_t pane_index)>;

        /**
         * @brief Synchronization mode between panes
         */
        enum class SyncMode
        {
            None,           // Independent navigation
            Navigate,       // Sync navigation only
            Selection,      // Sync selection only
            Full            // Sync both navigation and selection
        };

        LayoutManager(std::shared_ptr<filesystem::FileSystemManager> fs_manager);
        ~LayoutManager();

        // Prevent copying
        LayoutManager(const LayoutManager&) = delete;
        LayoutManager& operator=(const LayoutManager&) = delete;

        /**
         * @brief Initialize with default single-pane layout
         */
        void Initialize(const std::string& initial_path = "");

        /**
         * @brief Set the layout type
         */
        void SetLayout(LayoutType layout);
        LayoutType GetLayout() const { return layout_; }

        /**
         * @brief Get the number of visible panes for current layout
         */
        size_t GetVisiblePaneCount() const;

        /**
         * @brief Get the focused pane index
         */
        size_t GetFocusedPaneIndex() const { return focused_pane_; }

        /**
         * @brief Set the focused pane
         */
        void SetFocusedPane(size_t index);

        /**
         * @brief Get TabManager for a specific pane
         */
        TabManager* GetPaneTabManager(size_t index);
        const TabManager* GetPaneTabManager(size_t index) const;

        /**
         * @brief Get the focused TabManager
         */
        TabManager* GetFocusedTabManager();
        const TabManager* GetFocusedTabManager() const;

        /**
         * @brief Get the focused FilePane
         */
        FilePane* GetFocusedPane();
        const FilePane* GetFocusedPane() const;

        // Split ratios (0.0 to 1.0)
        void SetHorizontalSplit(float ratio) { h_split_ratio_ = std::clamp(ratio, 0.1f, 0.9f); }
        void SetVerticalSplit(float ratio) { v_split_ratio_ = std::clamp(ratio, 0.1f, 0.9f); }
        float GetHorizontalSplit() const { return h_split_ratio_; }
        float GetVerticalSplit() const { return v_split_ratio_; }

        // Synchronization
        void SetSyncMode(SyncMode mode) { sync_mode_ = mode; }
        SyncMode GetSyncMode() const { return sync_mode_; }

        /**
         * @brief Swap contents of two panes
         */
        void SwapPanes(size_t pane1, size_t pane2);

        /**
         * @brief Move focused item(s) to the other pane
         */
        void MoveToOtherPane();

        /**
         * @brief Copy focused item(s) to the other pane
         */
        void CopyToOtherPane();

        /**
         * @brief Cycle focus to next pane
         */
        void FocusNextPane();

        /**
         * @brief Cycle focus to previous pane
         */
        void FocusPreviousPane();

        /**
         * @brief Toggle between single and dual-pane layout
         */
        void ToggleDualPane();

        /**
         * @brief Render all panes according to current layout
         * @param width Available width
         * @param height Available height
         */
        void Render(float width, float height);

        /**
         * @brief Handle keyboard shortcuts for layout/pane management
         */
        void HandleKeyboardInput();

        /**
         * @brief Set callback for when focused pane changes
         */
        void SetFocusChangedCallback(FocusChangedCallback callback) { on_focus_changed_ = std::move(callback); }

        // Layout presets
        void LoadLayoutPreset(const std::string& name);
        void SaveLayoutPreset(const std::string& name);
        std::vector<std::string> GetLayoutPresets() const;

    private:
        void RenderSingleLayout(float width, float height);
        void RenderDualHorizontalLayout(float width, float height);
        void RenderDualVerticalLayout(float width, float height);
        void RenderTripleLeftLayout(float width, float height);
        void RenderTripleRightLayout(float width, float height);
        void RenderQuadLayout(float width, float height);

        void RenderPaneWithBorder(size_t pane_index, float x, float y, float width, float height);
        void HandlePaneSynchronization(size_t source_pane);
        void EnsurePanesExist(size_t count);

        std::shared_ptr<filesystem::FileSystemManager> fs_manager_;
        std::array<std::unique_ptr<TabManager>, MAX_PANES> panes_;
        
        LayoutType layout_ = LayoutType::Single;
        size_t focused_pane_ = 0;
        
        float h_split_ratio_ = 0.5f;
        float v_split_ratio_ = 0.5f;
        
        SyncMode sync_mode_ = SyncMode::None;
        
        FocusChangedCallback on_focus_changed_;

        // Splitter drag state
        bool dragging_h_split_ = false;
        bool dragging_v_split_ = false;
    };

} // namespace opacity::ui
