#include "opacity/ui/LayoutManager.h"
#include "opacity/core/Logger.h"

#include <imgui.h>
#include <algorithm>

namespace opacity::ui
{
    LayoutManager::LayoutManager(std::shared_ptr<filesystem::FileSystemManager> fs_manager)
        : fs_manager_(std::move(fs_manager))
    {
        SPDLOG_DEBUG("LayoutManager created");
    }

    LayoutManager::~LayoutManager()
    {
        SPDLOG_DEBUG("LayoutManager destroyed");
    }

    void LayoutManager::Initialize(const std::string& initial_path)
    {
        // Create the first pane
        panes_[0] = std::make_unique<TabManager>(fs_manager_);
        panes_[0]->CreateTab(initial_path);
        
        SPDLOG_INFO("LayoutManager initialized with single pane layout");
    }

    void LayoutManager::SetLayout(LayoutType layout)
    {
        if (layout_ == layout)
            return;

        LayoutType old_layout = layout_;
        layout_ = layout;

        size_t needed = GetVisiblePaneCount();
        EnsurePanesExist(needed);

        SPDLOG_INFO("Layout changed from {} to {}", static_cast<int>(old_layout), static_cast<int>(layout));
    }

    size_t LayoutManager::GetVisiblePaneCount() const
    {
        switch (layout_)
        {
        case LayoutType::Single:
            return 1;
        case LayoutType::DualHorizontal:
        case LayoutType::DualVertical:
            return 2;
        case LayoutType::TripleLeft:
        case LayoutType::TripleRight:
            return 3;
        case LayoutType::Quad:
            return 4;
        default:
            return 1;
        }
    }

    void LayoutManager::SetFocusedPane(size_t index)
    {
        if (index < GetVisiblePaneCount() && panes_[index])
        {
            focused_pane_ = index;
            if (on_focus_changed_)
                on_focus_changed_(index);
        }
    }

    TabManager* LayoutManager::GetPaneTabManager(size_t index)
    {
        if (index < MAX_PANES)
            return panes_[index].get();
        return nullptr;
    }

    const TabManager* LayoutManager::GetPaneTabManager(size_t index) const
    {
        if (index < MAX_PANES)
            return panes_[index].get();
        return nullptr;
    }

    TabManager* LayoutManager::GetFocusedTabManager()
    {
        return GetPaneTabManager(focused_pane_);
    }

    const TabManager* LayoutManager::GetFocusedTabManager() const
    {
        return GetPaneTabManager(focused_pane_);
    }

    FilePane* LayoutManager::GetFocusedPane()
    {
        if (TabManager* tm = GetFocusedTabManager())
            return tm->GetActivePane();
        return nullptr;
    }

    const FilePane* LayoutManager::GetFocusedPane() const
    {
        if (const TabManager* tm = GetFocusedTabManager())
            return tm->GetActivePane();
        return nullptr;
    }

    void LayoutManager::SwapPanes(size_t pane1, size_t pane2)
    {
        if (pane1 >= MAX_PANES || pane2 >= MAX_PANES || pane1 == pane2)
            return;

        std::swap(panes_[pane1], panes_[pane2]);
    }

    void LayoutManager::MoveToOtherPane()
    {
        if (layout_ == LayoutType::Single)
            return;

        size_t other = (focused_pane_ + 1) % GetVisiblePaneCount();
        
        FilePane* source = GetFocusedPane();
        FilePane* dest = nullptr;
        if (TabManager* tm = GetPaneTabManager(other))
            dest = tm->GetActivePane();

        if (!source || !dest)
            return;

        auto selected = source->GetSelectedItems();
        if (selected.empty())
            return;

        std::string dest_path = dest->GetCurrentPath();
        
        for (const auto& item : selected)
        {
            core::Path dest_item(dest_path);
            // Append filename to destination
            std::string full_dest = dest_path + "\\" + item.name;
            
            if (fs_manager_->Move(item.full_path, core::Path(full_dest)))
            {
                SPDLOG_INFO("Moved {} to {}", item.full_path.String(), full_dest);
            }
        }

        source->Refresh();
        dest->Refresh();
    }

    void LayoutManager::CopyToOtherPane()
    {
        if (layout_ == LayoutType::Single)
            return;

        size_t other = (focused_pane_ + 1) % GetVisiblePaneCount();
        
        FilePane* source = GetFocusedPane();
        FilePane* dest = nullptr;
        if (TabManager* tm = GetPaneTabManager(other))
            dest = tm->GetActivePane();

        if (!source || !dest)
            return;

        auto selected = source->GetSelectedItems();
        if (selected.empty())
            return;

        std::string dest_path = dest->GetCurrentPath();
        
        for (const auto& item : selected)
        {
            std::string full_dest = dest_path + "\\" + item.name;
            
            if (fs_manager_->Copy(item.full_path, core::Path(full_dest)))
            {
                SPDLOG_INFO("Copied {} to {}", item.full_path.String(), full_dest);
            }
        }

        dest->Refresh();
    }

    void LayoutManager::FocusNextPane()
    {
        size_t count = GetVisiblePaneCount();
        if (count <= 1)
            return;

        SetFocusedPane((focused_pane_ + 1) % count);
    }

    void LayoutManager::FocusPreviousPane()
    {
        size_t count = GetVisiblePaneCount();
        if (count <= 1)
            return;

        if (focused_pane_ == 0)
            SetFocusedPane(count - 1);
        else
            SetFocusedPane(focused_pane_ - 1);
    }

    void LayoutManager::ToggleDualPane()
    {
        if (layout_ == LayoutType::Single)
        {
            SetLayout(LayoutType::DualHorizontal);
        }
        else
        {
            SetLayout(LayoutType::Single);
        }
    }

    void LayoutManager::Render(float width, float height)
    {
        switch (layout_)
        {
        case LayoutType::Single:
            RenderSingleLayout(width, height);
            break;
        case LayoutType::DualHorizontal:
            RenderDualHorizontalLayout(width, height);
            break;
        case LayoutType::DualVertical:
            RenderDualVerticalLayout(width, height);
            break;
        case LayoutType::TripleLeft:
            RenderTripleLeftLayout(width, height);
            break;
        case LayoutType::TripleRight:
            RenderTripleRightLayout(width, height);
            break;
        case LayoutType::Quad:
            RenderQuadLayout(width, height);
            break;
        }
    }

    void LayoutManager::RenderSingleLayout(float width, float height)
    {
        RenderPaneWithBorder(0, 0, 0, width, height);
    }

    void LayoutManager::RenderDualHorizontalLayout(float width, float height)
    {
        float splitter_width = 4.0f;
        float left_width = width * h_split_ratio_ - splitter_width / 2;
        float right_width = width * (1.0f - h_split_ratio_) - splitter_width / 2;

        // Left pane
        RenderPaneWithBorder(0, 0, 0, left_width, height);

        // Splitter
        ImGui::SetCursorPos(ImVec2(left_width, 0));
        ImGui::InvisibleButton("##HSplitter", ImVec2(splitter_width, height));
        
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        
        if (ImGui::IsItemActive())
        {
            float delta = ImGui::GetIO().MouseDelta.x;
            h_split_ratio_ = std::clamp(h_split_ratio_ + delta / width, 0.1f, 0.9f);
        }

        // Draw splitter line
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 splitter_pos = ImGui::GetWindowPos();
        splitter_pos.x += left_width + splitter_width / 2;
        draw_list->AddLine(
            splitter_pos,
            ImVec2(splitter_pos.x, splitter_pos.y + height),
            IM_COL32(128, 128, 128, 255),
            2.0f
        );

        // Right pane
        RenderPaneWithBorder(1, left_width + splitter_width, 0, right_width, height);
    }

    void LayoutManager::RenderDualVerticalLayout(float width, float height)
    {
        float splitter_height = 4.0f;
        float top_height = height * v_split_ratio_ - splitter_height / 2;
        float bottom_height = height * (1.0f - v_split_ratio_) - splitter_height / 2;

        // Top pane
        RenderPaneWithBorder(0, 0, 0, width, top_height);

        // Splitter
        ImGui::SetCursorPos(ImVec2(0, top_height));
        ImGui::InvisibleButton("##VSplitter", ImVec2(width, splitter_height));
        
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        
        if (ImGui::IsItemActive())
        {
            float delta = ImGui::GetIO().MouseDelta.y;
            v_split_ratio_ = std::clamp(v_split_ratio_ + delta / height, 0.1f, 0.9f);
        }

        // Draw splitter line
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 splitter_pos = ImGui::GetWindowPos();
        splitter_pos.y += top_height + splitter_height / 2;
        draw_list->AddLine(
            splitter_pos,
            ImVec2(splitter_pos.x + width, splitter_pos.y),
            IM_COL32(128, 128, 128, 255),
            2.0f
        );

        // Bottom pane
        RenderPaneWithBorder(1, 0, top_height + splitter_height, width, bottom_height);
    }

    void LayoutManager::RenderTripleLeftLayout(float width, float height)
    {
        float splitter_width = 4.0f;
        float left_width = width * h_split_ratio_ - splitter_width / 2;
        float right_width = width * (1.0f - h_split_ratio_) - splitter_width / 2;
        float half_height = height / 2.0f - 2.0f;

        // Large left pane
        RenderPaneWithBorder(0, 0, 0, left_width, height);

        // Right top pane
        RenderPaneWithBorder(1, left_width + splitter_width, 0, right_width, half_height);

        // Right bottom pane
        RenderPaneWithBorder(2, left_width + splitter_width, half_height + 4.0f, right_width, half_height);
    }

    void LayoutManager::RenderTripleRightLayout(float width, float height)
    {
        float splitter_width = 4.0f;
        float left_width = width * h_split_ratio_ - splitter_width / 2;
        float right_width = width * (1.0f - h_split_ratio_) - splitter_width / 2;
        float half_height = height / 2.0f - 2.0f;

        // Left top pane
        RenderPaneWithBorder(0, 0, 0, left_width, half_height);

        // Left bottom pane
        RenderPaneWithBorder(1, 0, half_height + 4.0f, left_width, half_height);

        // Large right pane
        RenderPaneWithBorder(2, left_width + splitter_width, 0, right_width, height);
    }

    void LayoutManager::RenderQuadLayout(float width, float height)
    {
        float splitter_size = 4.0f;
        float half_width = width / 2.0f - splitter_size / 2;
        float half_height = height / 2.0f - splitter_size / 2;

        // Top-left
        RenderPaneWithBorder(0, 0, 0, half_width, half_height);

        // Top-right
        RenderPaneWithBorder(1, half_width + splitter_size, 0, half_width, half_height);

        // Bottom-left
        RenderPaneWithBorder(2, 0, half_height + splitter_size, half_width, half_height);

        // Bottom-right
        RenderPaneWithBorder(3, half_width + splitter_size, half_height + splitter_size, half_width, half_height);
    }

    void LayoutManager::RenderPaneWithBorder(size_t pane_index, float x, float y, float width, float height)
    {
        if (pane_index >= MAX_PANES || !panes_[pane_index])
            return;

        bool is_focused = (pane_index == focused_pane_);

        // Set cursor position
        ImGui::SetCursorPos(ImVec2(x, y));

        // Draw focus border
        if (is_focused && GetVisiblePaneCount() > 1)
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetWindowPos();
            pos.x += x;
            pos.y += y;
            draw_list->AddRect(
                pos,
                ImVec2(pos.x + width, pos.y + height),
                IM_COL32(100, 149, 237, 255),
                0.0f,
                0,
                2.0f
            );
        }

        std::string child_id = "##Pane" + std::to_string(pane_index);
        ImGui::BeginChild(child_id.c_str(), ImVec2(width, height), true);

        // Check for click to set focus
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(0))
        {
            if (!is_focused)
                SetFocusedPane(pane_index);
        }

        panes_[pane_index]->Render(width, height);

        ImGui::EndChild();
    }

    void LayoutManager::HandleKeyboardInput()
    {
        ImGuiIO& io = ImGui::GetIO();

        if (!io.WantTextInput)
        {
            // F6 or Tab (without Ctrl) to switch panes
            if (ImGui::IsKeyPressed(ImGuiKey_F6))
            {
                FocusNextPane();
            }

            // Ctrl+Shift+O to toggle dual pane
            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O))
            {
                ToggleDualPane();
            }

            // Alt+1/2/3/4 to focus specific pane
            if (io.KeyAlt)
            {
                for (int i = 1; i <= 4; ++i)
                {
                    ImGuiKey key = static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_1) + i - 1);
                    if (ImGui::IsKeyPressed(key) && static_cast<size_t>(i) <= GetVisiblePaneCount())
                    {
                        SetFocusedPane(i - 1);
                    }
                }
            }

            // Forward keyboard input to focused pane
            if (FilePane* pane = GetFocusedPane())
            {
                pane->HandleKeyboardInput();
            }
        }
    }

    void LayoutManager::HandlePaneSynchronization(size_t source_pane)
    {
        if (sync_mode_ == SyncMode::None)
            return;

        FilePane* source = nullptr;
        if (TabManager* tm = GetPaneTabManager(source_pane))
            source = tm->GetActivePane();

        if (!source)
            return;

        for (size_t i = 0; i < GetVisiblePaneCount(); ++i)
        {
            if (i == source_pane)
                continue;

            FilePane* target = nullptr;
            if (TabManager* tm = GetPaneTabManager(i))
                target = tm->GetActivePane();

            if (!target)
                continue;

            if (sync_mode_ == SyncMode::Navigate || sync_mode_ == SyncMode::Full)
            {
                target->NavigateTo(source->GetCurrentPath());
            }

            // Selection sync would be more complex - skip for now
        }
    }

    void LayoutManager::EnsurePanesExist(size_t count)
    {
        for (size_t i = 0; i < count && i < MAX_PANES; ++i)
        {
            if (!panes_[i])
            {
                panes_[i] = std::make_unique<TabManager>(fs_manager_);
                
                // Copy path from first pane if available
                std::string path;
                if (panes_[0] && panes_[0]->GetActivePane())
                {
                    path = panes_[0]->GetActivePane()->GetCurrentPath();
                }
                
                panes_[i]->CreateTab(path);
            }
        }
    }

    void LayoutManager::LoadLayoutPreset(const std::string& name)
    {
        // TODO: Load from config file
        SPDLOG_INFO("Loading layout preset: {}", name);
    }

    void LayoutManager::SaveLayoutPreset(const std::string& name)
    {
        // TODO: Save to config file
        SPDLOG_INFO("Saving layout preset: {}", name);
    }

    std::vector<std::string> LayoutManager::GetLayoutPresets() const
    {
        // TODO: Read from config
        return {"Default", "Dual Pane", "Commander Style"};
    }

} // namespace opacity::ui
