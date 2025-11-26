// Simple compile-time/test-time checks for ImGui RAII wrappers
#include "opacity/ui/ImGuiScoped.h"
#include <type_traits>
#include <iostream>

int main()
{
    static_assert(std::is_destructible_v<opacity::ui::ImGuiScopedGroup>, "ImGuiScopedGroup must be destructible");
    static_assert(std::is_constructible_v<opacity::ui::ImGuiScopedID, int>, "ImGuiScopedID should be constructible with int");
    std::cout << "ImGui RAII compile-time checks passed.\n";
    return 0;
}
