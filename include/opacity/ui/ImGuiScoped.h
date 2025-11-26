// RAII helpers for ImGui scope management
#pragma once

#include <imgui.h>
#ifndef NDEBUG
#include "imgui_internal.h" // Access to internal ID stack for debug checking
#endif

namespace opacity::ui
{

    struct ImGuiScopedID
    {
        ImGuiScopedID(int id)
        {
            if (ImGui::GetCurrentContext())
            {
                ImGui::PushID(id);
                pushed_ = true;
            }
        }

        ImGuiScopedID(const char* id)
        {
            if (ImGui::GetCurrentContext())
            {
                ImGui::PushID(id);
                pushed_ = true;
            }
        }

        ~ImGuiScopedID()
        {
            if (pushed_ && ImGui::GetCurrentContext())
            {
                ImGui::PopID();
            }
        }

    private:
        bool pushed_ = false;
    };


    struct ImGuiScopedGroup
    {
        ImGuiScopedGroup()
        {
            if (ImGui::GetCurrentContext())
            {
                ImGui::BeginGroup();
                begun_ = true;
            }
        }

        ~ImGuiScopedGroup()
        {
            if (begun_ && ImGui::GetCurrentContext())
            {
                ImGui::EndGroup();
            }
        }

    private:
        bool begun_ = false;
    };

#ifndef NDEBUG
    // Debug-only helper to ensure the ID stack is balanced after a scope.
    struct ImGuiIdStackChecker
    {
        ImGuiIdStackChecker()
        {
            ImGuiWindow* w = ImGui::GetCurrentWindow();
            start_ = w ? w->IDStack.Size : 0;
        }

        ~ImGuiIdStackChecker()
        {
            ImGuiWindow* w = ImGui::GetCurrentWindow();
            int cur = w ? w->IDStack.Size : 0;
            IM_ASSERT(start_ == cur && "ImGui IDStack mismatch detected");
        }

    private:
        int start_ = 0;
    };
#else
    struct ImGuiIdStackChecker { ImGuiIdStackChecker(){} }; // no-op on release builds
#endif

} // namespace opacity::ui
