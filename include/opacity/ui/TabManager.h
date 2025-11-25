#pragma once

#include "opacity/ui/FilePane.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <optional>

namespace opacity::ui
{
    /**
     * @brief Represents a single tab containing a FilePane
     */
    struct Tab
    {
        std::unique_ptr<FilePane> pane;
        bool is_pinned = false;
        bool is_modified = false;  // For future: track unsaved changes
        std::string color;         // Optional tab color (hex string)
        
        Tab() = default;
        Tab(std::unique_ptr<FilePane> p) : pane(std::move(p)) {}
        
        // Move only
        Tab(Tab&&) = default;
        Tab& operator=(Tab&&) = default;
    };

    /**
     * @brief Manages a collection of tabs, each containing a FilePane
     * 
     * Features:
     * - Create, close, duplicate, and reorder tabs
     * - Tab pinning and custom colors
     * - Recently closed tab history for restoration
     * - Tab group management
     */
    class TabManager
    {
    public:
        using TabId = FilePane::PaneId;
        using TabChangedCallback = std::function<void(TabId active_tab)>;

        TabManager(std::shared_ptr<filesystem::FileSystemManager> fs_manager);
        ~TabManager();

        // Prevent copying
        TabManager(const TabManager&) = delete;
        TabManager& operator=(const TabManager&) = delete;

        /**
         * @brief Create a new tab
         * @param path Initial path for the tab (empty = home directory)
         * @param make_active Make this the active tab
         * @return ID of the created tab
         */
        TabId CreateTab(const std::string& path = "", bool make_active = true);

        /**
         * @brief Close a tab
         * @param id Tab to close
         * @return true if tab was closed
         */
        bool CloseTab(TabId id);

        /**
         * @brief Close all tabs except the specified one
         */
        void CloseOtherTabs(TabId keep);

        /**
         * @brief Close all tabs to the right of the specified tab
         */
        void CloseTabsToRight(TabId id);

        /**
         * @brief Duplicate a tab
         * @param id Tab to duplicate
         * @return ID of the new tab
         */
        TabId DuplicateTab(TabId id);

        /**
         * @brief Reopen the last closed tab
         * @return ID of reopened tab, or nullopt if none available
         */
        std::optional<TabId> ReopenClosedTab();

        /**
         * @brief Move a tab to a new position
         * @param id Tab to move
         * @param new_index New position index
         */
        void MoveTab(TabId id, size_t new_index);

        /**
         * @brief Pin or unpin a tab
         */
        void SetTabPinned(TabId id, bool pinned);

        /**
         * @brief Set custom tab color
         * @param id Tab to color
         * @param color Hex color string (e.g., "#FF5733")
         */
        void SetTabColor(TabId id, const std::string& color);

        /**
         * @brief Set custom tab name
         */
        void SetTabName(TabId id, const std::string& name);

        // Active tab management
        void SetActiveTab(TabId id);
        TabId GetActiveTabId() const;
        FilePane* GetActivePane();
        const FilePane* GetActivePane() const;

        // Tab access
        FilePane* GetPane(TabId id);
        const FilePane* GetPane(TabId id) const;
        Tab* GetTab(TabId id);
        const Tab* GetTab(TabId id) const;

        size_t GetTabCount() const { return tabs_.size(); }
        bool IsEmpty() const { return tabs_.empty(); }

        /**
         * @brief Get all tabs in order
         */
        const std::vector<Tab>& GetTabs() const { return tabs_; }

        /**
         * @brief Render the tab bar and active pane
         * @param width Available width
         * @param height Available height
         */
        void Render(float width, float height);

        /**
         * @brief Set callback for when active tab changes
         */
        void SetTabChangedCallback(TabChangedCallback callback) { on_tab_changed_ = std::move(callback); }

        /**
         * @brief Navigate to next tab
         */
        void NextTab();

        /**
         * @brief Navigate to previous tab
         */
        void PreviousTab();

        /**
         * @brief Switch to tab by index (1-9 for tabs 1-9, 0 for tab 10)
         */
        void SwitchToTab(int index);

    private:
        void RenderTabBar();
        void RenderTabContextMenu(size_t tab_index);
        size_t FindTabIndex(TabId id) const;
        void EnsureActiveTabValid();

        std::shared_ptr<filesystem::FileSystemManager> fs_manager_;
        std::vector<Tab> tabs_;
        size_t active_tab_index_ = 0;

        // Recently closed tabs for restoration
        struct ClosedTabInfo
        {
            std::string path;
            bool was_pinned;
            std::string color;
        };
        std::vector<ClosedTabInfo> closed_tabs_;
        static constexpr size_t MAX_CLOSED_TABS = 10;

        TabChangedCallback on_tab_changed_;
    };

} // namespace opacity::ui
