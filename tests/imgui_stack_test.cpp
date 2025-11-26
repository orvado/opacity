// Simple test to verify ImGui RAII wrappers maintain ID stack balance
#include <imgui.h>
#include "imgui_internal.h"
#include "opacity/ui/ImGuiScoped.h"
#include <cassert>
#include <iostream>

int main()
{
    // Create a context (no backend required for ID stack check)
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    std::cout << "DisplaySize set to: " << io.DisplaySize.x << ", " << io.DisplaySize.y << std::endl;
    // Minimal state so NewFrame/Begin can create a window
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::Begin("TestWindow");

    auto* w = ImGui::GetCurrentWindow();
    int before = w->IDStack.Size;

    {
        opacity::ui::ImGuiScopedID id1(123);
        assert(w->IDStack.Size == before + 1);
        {
            opacity::ui::ImGuiScopedGroup group;
            // group doesn't alter ID stack but demonstrates RAII
        }
    }

    int after = w->IDStack.Size;
    assert(before == after);

    ImGui::End();
    ImGui::DestroyContext(ctx);

    std::cout << "ImGui RAII test passed.\n";
    return 0;
}
