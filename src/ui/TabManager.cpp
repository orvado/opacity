#include "opacity/ui/TabManager.h"
#include "opacity/core/Logger.h"

#include <imgui.h>
#include <algorithm>

namespace opacity::ui
{
    TabManager::TabManager(std::shared_ptr<filesystem::FileSystemManager> fs_manager)
        : fs_manager_(std::move(fs_manager))
    {
        SPDLOG_DEBUG("TabManager created");
    }

    TabManager::~TabManager()
    {
        SPDLOG_DEBUG("TabManager destroyed");
    }

    TabManager::TabId TabManager::CreateTab(const std::string& path, bool make_active)
    {
        auto pane = std::make_unique<FilePane>(fs_manager_);
        
        std::string initial_path = path;
        if (initial_path.empty())
        {
            initial_path = fs_manager_->GetUserHomeDirectory();
            if (initial_path.empty())
                initial_path = "C:\\";
        }
        
        pane->NavigateTo(initial_path);
        
        TabId id = pane->GetId();
        tabs_.emplace_back(std::move(pane));
        
        SPDLOG_INFO("Created new tab {} at path: {}", id.id, initial_path);
        
        if (make_active)
        {
            active_tab_index_ = tabs_.size() - 1;
            if (on_tab_changed_)
                on_tab_changed_(id);
        }
        
        return id;
    }

    bool TabManager::CloseTab(TabId id)
    {
        size_t index = FindTabIndex(id);
        if (index == SIZE_MAX)
            return false;
        
        // Don't close pinned tabs without confirmation
        // For now, allow closing but save to history
        
        // Save to closed tabs history
        if (tabs_[index].pane)
        {
            ClosedTabInfo info;
            info.path = tabs_[index].pane->GetCurrentPath();
            info.was_pinned = tabs_[index].is_pinned;
            info.color = tabs_[index].color;
            
            closed_tabs_.push_back(info);
            if (closed_tabs_.size() > MAX_CLOSED_TABS)
                closed_tabs_.erase(closed_tabs_.begin());
        }
        
        SPDLOG_INFO("Closing tab {} at index {}", id.id, index);
        
        tabs_.erase(tabs_.begin() + index);
        
        // Adjust active tab index
        if (tabs_.empty())
        {
            // Create a new default tab
            CreateTab();
        }
        else
        {
            if (active_tab_index_ >= tabs_.size())
                active_tab_index_ = tabs_.size() - 1;
        }
        
        if (on_tab_changed_ && !tabs_.empty())
            on_tab_changed_(GetActiveTabId());
        
        return true;
    }

    void TabManager::CloseOtherTabs(TabId keep)
    {
        size_t keep_index = FindTabIndex(keep);
        if (keep_index == SIZE_MAX)
            return;
        
        // Close tabs after the kept one (in reverse order)
        for (size_t i = tabs_.size(); i > keep_index + 1; --i)
        {
            if (!tabs_[i - 1].is_pinned)
            {
                CloseTab(tabs_[i - 1].pane->GetId());
            }
        }
        
        // Close tabs before the kept one (in reverse order)
        for (size_t i = keep_index; i > 0; --i)
        {
            if (!tabs_[i - 1].is_pinned)
            {
                CloseTab(tabs_[i - 1].pane->GetId());
            }
        }
    }

    void TabManager::CloseTabsToRight(TabId id)
    {
        size_t index = FindTabIndex(id);
        if (index == SIZE_MAX)
            return;
        
        for (size_t i = tabs_.size(); i > index + 1; --i)
        {
            if (!tabs_[i - 1].is_pinned)
            {
                CloseTab(tabs_[i - 1].pane->GetId());
            }
        }
    }

    TabManager::TabId TabManager::DuplicateTab(TabId id)
    {
        size_t index = FindTabIndex(id);
        if (index == SIZE_MAX)
            return CreateTab();
        
        const auto& source = tabs_[index];
        std::string path = source.pane ? source.pane->GetCurrentPath() : "";
        
        TabId new_id = CreateTab(path, true);
        
        // Copy settings
        if (Tab* new_tab = GetTab(new_id))
        {
            new_tab->color = source.color;
            // Copy view settings
            if (source.pane && new_tab->pane)
            {
                new_tab->pane->SetViewMode(source.pane->GetViewMode());
                new_tab->pane->SetIconSize(source.pane->GetIconSize());
                new_tab->pane->SetShowHidden(source.pane->GetShowHidden());
            }
        }
        
        return new_id;
    }

    std::optional<TabManager::TabId> TabManager::ReopenClosedTab()
    {
        if (closed_tabs_.empty())
            return std::nullopt;
        
        ClosedTabInfo info = closed_tabs_.back();
        closed_tabs_.pop_back();
        
        TabId id = CreateTab(info.path, true);
        
        if (Tab* tab = GetTab(id))
        {
            tab->is_pinned = info.was_pinned;
            tab->color = info.color;
        }
        
        SPDLOG_INFO("Reopened closed tab at: {}", info.path);
        
        return id;
    }

    void TabManager::MoveTab(TabId id, size_t new_index)
    {
        size_t current_index = FindTabIndex(id);
        if (current_index == SIZE_MAX || new_index >= tabs_.size())
            return;
        
        if (current_index == new_index)
            return;
        
        Tab tab = std::move(tabs_[current_index]);
        tabs_.erase(tabs_.begin() + current_index);
        tabs_.insert(tabs_.begin() + new_index, std::move(tab));
        
        // Adjust active tab index
        if (active_tab_index_ == current_index)
        {
            active_tab_index_ = new_index;
        }
        else if (current_index < active_tab_index_ && new_index >= active_tab_index_)
        {
            --active_tab_index_;
        }
        else if (current_index > active_tab_index_ && new_index <= active_tab_index_)
        {
            ++active_tab_index_;
        }
    }

    void TabManager::SetTabPinned(TabId id, bool pinned)
    {
        if (Tab* tab = GetTab(id))
        {
            tab->is_pinned = pinned;
            
            // Move pinned tabs to the left
            if (pinned)
            {
                size_t index = FindTabIndex(id);
                size_t first_unpinned = 0;
                for (size_t i = 0; i < tabs_.size(); ++i)
                {
                    if (!tabs_[i].is_pinned)
                    {
                        first_unpinned = i;
                        break;
                    }
                }
                if (index > first_unpinned)
                {
                    MoveTab(id, first_unpinned);
                }
            }
        }
    }

    void TabManager::SetTabColor(TabId id, const std::string& color)
    {
        if (Tab* tab = GetTab(id))
        {
            tab->color = color;
        }
    }

    void TabManager::SetTabName(TabId id, const std::string& name)
    {
        if (Tab* tab = GetTab(id))
        {
            if (tab->pane)
                tab->pane->SetTitle(name);
        }
    }

    void TabManager::SetActiveTab(TabId id)
    {
        size_t index = FindTabIndex(id);
        if (index != SIZE_MAX && index != active_tab_index_)
        {
            active_tab_index_ = index;
            if (on_tab_changed_)
                on_tab_changed_(id);
        }
    }

    TabManager::TabId TabManager::GetActiveTabId() const
    {
        if (active_tab_index_ < tabs_.size() && tabs_[active_tab_index_].pane)
        {
            return tabs_[active_tab_index_].pane->GetId();
        }
        return TabId{0};
    }

    FilePane* TabManager::GetActivePane()
    {
        if (active_tab_index_ < tabs_.size())
            return tabs_[active_tab_index_].pane.get();
        return nullptr;
    }

    const FilePane* TabManager::GetActivePane() const
    {
        if (active_tab_index_ < tabs_.size())
            return tabs_[active_tab_index_].pane.get();
        return nullptr;
    }

    FilePane* TabManager::GetPane(TabId id)
    {
        if (Tab* tab = GetTab(id))
            return tab->pane.get();
        return nullptr;
    }

    const FilePane* TabManager::GetPane(TabId id) const
    {
        if (const Tab* tab = GetTab(id))
            return tab->pane.get();
        return nullptr;
    }

    Tab* TabManager::GetTab(TabId id)
    {
        size_t index = FindTabIndex(id);
        if (index != SIZE_MAX)
            return &tabs_[index];
        return nullptr;
    }

    const Tab* TabManager::GetTab(TabId id) const
    {
        size_t index = FindTabIndex(id);
        if (index != SIZE_MAX)
            return &tabs_[index];
        return nullptr;
    }

    void TabManager::Render(float width, float height)
    {
        RenderTabBar();
        
        // Render active pane
        float remaining_height = height - ImGui::GetFrameHeightWithSpacing();
        
        if (FilePane* pane = GetActivePane())
        {
            ImGui::BeginChild("##TabContent", ImVec2(width, remaining_height), false);
            pane->Render(width, remaining_height);
            ImGui::EndChild();
        }
    }

    void TabManager::RenderTabBar()
    {
        ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_Reorderable |
                                         ImGuiTabBarFlags_TabListPopupButton |
                                         ImGuiTabBarFlags_FittingPolicyScroll;

        if (ImGui::BeginTabBar("##Tabs", tab_bar_flags))
        {
            // Add tab button
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
            {
                CreateTab();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("New Tab (Ctrl+T)");

            for (size_t i = 0; i < tabs_.size(); ++i)
            {
                Tab& tab = tabs_[i];
                if (!tab.pane)
                    continue;

                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                // Note: Pinned tabs don't have close button - we pass nullptr for p_open

                bool open = true;
                std::string title = tab.pane->GetTitle();
                
                if (tab.is_pinned)
                    title = "* " + title;

                // Apply custom color if set
                bool has_color = !tab.color.empty();
                if (has_color)
                {
                    // Parse hex color and apply
                    unsigned int r = 100, g = 149, b = 237;
                    if (tab.color.size() == 7 && tab.color[0] == '#')
                    {
                        sscanf(tab.color.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
                    }
                    ImGui::PushStyleColor(ImGuiCol_Tab, IM_COL32(r, g, b, 200));
                    ImGui::PushStyleColor(ImGuiCol_TabHovered, IM_COL32(r, g, b, 255));
                    ImGui::PushStyleColor(ImGuiCol_TabActive, IM_COL32(r, g, b, 255));
                }

                bool is_active = (i == active_tab_index_);
                std::string tab_id = title + "##" + std::to_string(i);
                
                // Use nullptr for p_open on pinned tabs to hide close button
                bool* p_open = tab.is_pinned ? nullptr : &open;
                if (ImGui::BeginTabItem(tab_id.c_str(), p_open, flags))
                {
                    if (!is_active)
                    {
                        active_tab_index_ = i;
                        if (on_tab_changed_)
                            on_tab_changed_(tab.pane->GetId());
                    }
                    ImGui::EndTabItem();
                }

                // Context menu for tab
                if (ImGui::BeginPopupContextItem())
                {
                    RenderTabContextMenu(i);
                    ImGui::EndPopup();
                }

                if (has_color)
                {
                    ImGui::PopStyleColor(3);
                }

                // Handle tab close
                if (!open && !tab.is_pinned)
                {
                    CloseTab(tab.pane->GetId());
                    break; // Exit loop as tabs_ has changed
                }
            }

            ImGui::EndTabBar();
        }
    }

    void TabManager::RenderTabContextMenu(size_t tab_index)
    {
        if (tab_index >= tabs_.size())
            return;

        Tab& tab = tabs_[tab_index];
        TabId id = tab.pane ? tab.pane->GetId() : TabId{0};

        if (ImGui::MenuItem("New Tab", "Ctrl+T"))
        {
            CreateTab();
        }

        if (ImGui::MenuItem("Duplicate Tab"))
        {
            DuplicateTab(id);
        }

        ImGui::Separator();

        bool is_pinned = tab.is_pinned;
        if (ImGui::MenuItem(is_pinned ? "Unpin Tab" : "Pin Tab"))
        {
            SetTabPinned(id, !is_pinned);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Close Tab", "Ctrl+W", false, !is_pinned))
        {
            CloseTab(id);
        }

        if (ImGui::MenuItem("Close Other Tabs"))
        {
            CloseOtherTabs(id);
        }

        if (ImGui::MenuItem("Close Tabs to the Right"))
        {
            CloseTabsToRight(id);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reopen Closed Tab", "Ctrl+Shift+T", false, !closed_tabs_.empty()))
        {
            ReopenClosedTab();
        }

        ImGui::Separator();

        // Tab color submenu
        if (ImGui::BeginMenu("Set Color"))
        {
            struct ColorOption { const char* name; const char* hex; };
            ColorOption colors[] = {
                {"None", ""},
                {"Red", "#E74C3C"},
                {"Orange", "#E67E22"},
                {"Yellow", "#F1C40F"},
                {"Green", "#2ECC71"},
                {"Blue", "#3498DB"},
                {"Purple", "#9B59B6"},
                {"Pink", "#E91E63"},
            };

            for (const auto& color : colors)
            {
                if (ImGui::MenuItem(color.name))
                {
                    SetTabColor(id, color.hex);
                }
            }

            ImGui::EndMenu();
        }
    }

    void TabManager::NextTab()
    {
        if (tabs_.size() <= 1)
            return;

        active_tab_index_ = (active_tab_index_ + 1) % tabs_.size();
        if (on_tab_changed_)
            on_tab_changed_(GetActiveTabId());
    }

    void TabManager::PreviousTab()
    {
        if (tabs_.size() <= 1)
            return;

        if (active_tab_index_ == 0)
            active_tab_index_ = tabs_.size() - 1;
        else
            --active_tab_index_;

        if (on_tab_changed_)
            on_tab_changed_(GetActiveTabId());
    }

    void TabManager::SwitchToTab(int index)
    {
        size_t target = (index == 0) ? 9 : static_cast<size_t>(index - 1);
        if (target < tabs_.size())
        {
            active_tab_index_ = target;
            if (on_tab_changed_)
                on_tab_changed_(GetActiveTabId());
        }
    }

    size_t TabManager::FindTabIndex(TabId id) const
    {
        for (size_t i = 0; i < tabs_.size(); ++i)
        {
            if (tabs_[i].pane && tabs_[i].pane->GetId() == id)
                return i;
        }
        return SIZE_MAX;
    }

    void TabManager::EnsureActiveTabValid()
    {
        if (tabs_.empty())
        {
            CreateTab();
        }
        else if (active_tab_index_ >= tabs_.size())
        {
            active_tab_index_ = tabs_.size() - 1;
        }
    }

} // namespace opacity::ui
