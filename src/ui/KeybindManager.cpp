#include "opacity/ui/KeybindManager.h"
#include "opacity/core/Logger.h"

#include <imgui.h>
#include "opacity/ui/ImGuiScoped.h"
#include <algorithm>
#include <sstream>
#include <cctype>

namespace opacity::ui
{
    // Keybind implementation
    bool Keybind::IsPressed() const
    {
        if (key == 0)
            return false;

        ImGuiIO& io = ImGui::GetIO();

        // Check modifiers
        bool ctrl_match = ((modifiers & ModifierKeys::Ctrl) != ModifierKeys::None) == io.KeyCtrl;
        bool shift_match = ((modifiers & ModifierKeys::Shift) != ModifierKeys::None) == io.KeyShift;
        bool alt_match = ((modifiers & ModifierKeys::Alt) != ModifierKeys::None) == io.KeyAlt;
        bool super_match = ((modifiers & ModifierKeys::Super) != ModifierKeys::None) == io.KeySuper;

        if (!ctrl_match || !shift_match || !alt_match || !super_match)
            return false;

        return ImGui::IsKeyPressed(static_cast<ImGuiKey>(key));
    }

    std::string Keybind::ToString() const
    {
        if (key == 0)
            return "";

        std::string result;

        if ((modifiers & ModifierKeys::Ctrl) != ModifierKeys::None)
            result += "Ctrl+";
        if ((modifiers & ModifierKeys::Shift) != ModifierKeys::None)
            result += "Shift+";
        if ((modifiers & ModifierKeys::Alt) != ModifierKeys::None)
            result += "Alt+";
        if ((modifiers & ModifierKeys::Super) != ModifierKeys::None)
            result += "Win+";

        // Convert ImGuiKey to string
        const char* key_name = ImGui::GetKeyName(static_cast<ImGuiKey>(key));
        if (key_name)
            result += key_name;
        else
            result += "Unknown";

        return result;
    }

    std::optional<Keybind> Keybind::FromString(const std::string& str)
    {
        if (str.empty())
            return std::nullopt;

        Keybind result;
        std::string remaining = str;

        // Parse modifiers
        auto consume_modifier = [&](const std::string& mod, ModifierKeys flag) {
            size_t pos = remaining.find(mod + "+");
            if (pos == 0)
            {
                result.modifiers = result.modifiers | flag;
                remaining = remaining.substr(mod.length() + 1);
                return true;
            }
            return false;
        };

        while (consume_modifier("Ctrl", ModifierKeys::Ctrl) ||
               consume_modifier("Shift", ModifierKeys::Shift) ||
               consume_modifier("Alt", ModifierKeys::Alt) ||
               consume_modifier("Win", ModifierKeys::Super))
        {
            // Continue parsing modifiers
        }

        // Parse key (remaining string)
        if (remaining.empty())
            return std::nullopt;

        // Try to find matching key
        for (int i = static_cast<int>(ImGuiKey_NamedKey_BEGIN); i < static_cast<int>(ImGuiKey_NamedKey_END); ++i)
        {
            const char* name = ImGui::GetKeyName(static_cast<ImGuiKey>(i));
            if (name && remaining == name)
            {
                result.key = i;
                return result;
            }
        }

        return std::nullopt;
    }

    // KeybindManager implementation
    KeybindManager::KeybindManager() = default;

    void KeybindManager::Initialize()
    {
        RegisterDefaultCommands();
        SPDLOG_INFO("KeybindManager initialized with {} commands", commands_.size());
    }

    void KeybindManager::RegisterDefaultCommands()
    {
        // Navigation commands
        RegisterCommand({
            "navigation.back", "Navigate Back", "Go to previous location",
            CommandCategory::Navigation,
            {static_cast<int>(ImGuiKey_LeftArrow), ModifierKeys::Alt}
        }, nullptr);

        RegisterCommand({
            "navigation.forward", "Navigate Forward", "Go to next location",
            CommandCategory::Navigation,
            {static_cast<int>(ImGuiKey_RightArrow), ModifierKeys::Alt}
        }, nullptr);

        RegisterCommand({
            "navigation.up", "Navigate Up", "Go to parent directory",
            CommandCategory::Navigation,
            {static_cast<int>(ImGuiKey_UpArrow), ModifierKeys::Alt}
        }, nullptr);

        RegisterCommand({
            "navigation.refresh", "Refresh", "Refresh current directory",
            CommandCategory::Navigation,
            {static_cast<int>(ImGuiKey_F5), ModifierKeys::None}
        }, nullptr);

        // File operations
        RegisterCommand({
            "file.new_folder", "New Folder", "Create a new folder",
            CommandCategory::FileOperations,
            {static_cast<int>(ImGuiKey_N), ModifierKeys::Ctrl | ModifierKeys::Shift}
        }, nullptr);

        RegisterCommand({
            "file.copy", "Copy", "Copy selected items",
            CommandCategory::FileOperations,
            {static_cast<int>(ImGuiKey_C), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "file.cut", "Cut", "Cut selected items",
            CommandCategory::FileOperations,
            {static_cast<int>(ImGuiKey_X), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "file.paste", "Paste", "Paste items from clipboard",
            CommandCategory::FileOperations,
            {static_cast<int>(ImGuiKey_V), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "file.delete", "Delete", "Delete selected items",
            CommandCategory::FileOperations,
            {static_cast<int>(ImGuiKey_Delete), ModifierKeys::None}
        }, nullptr);

        RegisterCommand({
            "file.rename", "Rename", "Rename selected item",
            CommandCategory::FileOperations,
            {static_cast<int>(ImGuiKey_F2), ModifierKeys::None}
        }, nullptr);

        RegisterCommand({
            "file.open", "Open", "Open selected item",
            CommandCategory::FileOperations,
            {static_cast<int>(ImGuiKey_Enter), ModifierKeys::None}
        }, nullptr);

        // Selection
        RegisterCommand({
            "selection.all", "Select All", "Select all items",
            CommandCategory::Selection,
            {static_cast<int>(ImGuiKey_A), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "selection.invert", "Invert Selection", "Invert current selection",
            CommandCategory::Selection,
            {static_cast<int>(ImGuiKey_I), ModifierKeys::Ctrl}
        }, nullptr);

        // View
        RegisterCommand({
            "view.hidden", "Toggle Hidden Files", "Show/hide hidden files",
            CommandCategory::View,
            {static_cast<int>(ImGuiKey_H), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "view.preview", "Toggle Preview Panel", "Show/hide preview panel",
            CommandCategory::View,
            {static_cast<int>(ImGuiKey_P), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "view.details", "Details View", "Switch to details view",
            CommandCategory::View,
            {static_cast<int>(ImGuiKey_1), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "view.icons", "Icons View", "Switch to icons view",
            CommandCategory::View,
            {static_cast<int>(ImGuiKey_2), ModifierKeys::Ctrl}
        }, nullptr);

        // Tabs
        RegisterCommand({
            "tab.new", "New Tab", "Open a new tab",
            CommandCategory::Tabs,
            {static_cast<int>(ImGuiKey_T), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "tab.close", "Close Tab", "Close current tab",
            CommandCategory::Tabs,
            {static_cast<int>(ImGuiKey_W), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "tab.reopen", "Reopen Closed Tab", "Reopen last closed tab",
            CommandCategory::Tabs,
            {static_cast<int>(ImGuiKey_T), ModifierKeys::Ctrl | ModifierKeys::Shift}
        }, nullptr);

        RegisterCommand({
            "tab.next", "Next Tab", "Switch to next tab",
            CommandCategory::Tabs,
            {static_cast<int>(ImGuiKey_Tab), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "tab.previous", "Previous Tab", "Switch to previous tab",
            CommandCategory::Tabs,
            {static_cast<int>(ImGuiKey_Tab), ModifierKeys::Ctrl | ModifierKeys::Shift}
        }, nullptr);

        // Panes
        RegisterCommand({
            "pane.toggle_dual", "Toggle Dual Pane", "Toggle dual pane layout",
            CommandCategory::Panes,
            {static_cast<int>(ImGuiKey_O), ModifierKeys::Ctrl | ModifierKeys::Shift}
        }, nullptr);

        RegisterCommand({
            "pane.switch", "Switch Pane", "Switch focus to other pane",
            CommandCategory::Panes,
            {static_cast<int>(ImGuiKey_F6), ModifierKeys::None}
        }, nullptr);

        RegisterCommand({
            "pane.swap", "Swap Panes", "Swap left and right pane contents",
            CommandCategory::Panes,
            {static_cast<int>(ImGuiKey_U), ModifierKeys::Ctrl}
        }, nullptr);

        // Search
        RegisterCommand({
            "search.find", "Find", "Search in current directory",
            CommandCategory::Search,
            {static_cast<int>(ImGuiKey_F), ModifierKeys::Ctrl}
        }, nullptr);

        RegisterCommand({
            "search.advanced", "Advanced Search", "Open advanced search dialog",
            CommandCategory::Search,
            {static_cast<int>(ImGuiKey_F), ModifierKeys::Ctrl | ModifierKeys::Shift}
        }, nullptr);

        // Help
        RegisterCommand({
            "help.command_palette", "Command Palette", "Open command palette",
            CommandCategory::Help,
            {static_cast<int>(ImGuiKey_F1), ModifierKeys::None}
        }, nullptr);

        RegisterCommand({
            "help.shortcuts", "Keyboard Shortcuts", "Show keyboard shortcuts",
            CommandCategory::Help,
            {static_cast<int>(ImGuiKey_Slash), ModifierKeys::Ctrl}
        }, nullptr);
    }

    void KeybindManager::RegisterCommand(
        const CommandDef& def,
        CommandCallback callback,
        ConditionalCallback condition)
    {
        CommandBinding binding;
        binding.definition = def;
        binding.current_keybind = def.default_keybind;
        binding.callback = std::move(callback);
        binding.condition = std::move(condition);

        commands_[def.id] = std::move(binding);
    }

    void KeybindManager::UnregisterCommand(const std::string& command_id)
    {
        commands_.erase(command_id);
    }

    bool KeybindManager::ExecuteCommand(const std::string& command_id)
    {
        auto it = commands_.find(command_id);
        if (it == commands_.end())
            return false;

        const auto& binding = it->second;

        // Check condition if present
        if (binding.condition && !binding.condition())
            return false;

        // Execute callback if present
        if (binding.callback)
        {
            binding.callback();
            SPDLOG_DEBUG("Executed command: {}", command_id);
            return true;
        }

        return false;
    }

    bool KeybindManager::SetKeybind(const std::string& command_id, const Keybind& keybind)
    {
        auto it = commands_.find(command_id);
        if (it == commands_.end())
            return false;

        if (!it->second.definition.allow_rebind)
            return false;

        // Check for conflicts
        if (keybind.IsValid() && HasConflict(keybind, command_id))
        {
            SPDLOG_WARN("Keybind conflict detected for: {}", keybind.ToString());
            return false;
        }

        it->second.current_keybind = keybind;
        SPDLOG_INFO("Set keybind for {}: {}", command_id, keybind.ToString());
        return true;
    }

    std::optional<Keybind> KeybindManager::GetKeybind(const std::string& command_id) const
    {
        auto it = commands_.find(command_id);
        if (it != commands_.end())
            return it->second.current_keybind;
        return std::nullopt;
    }

    void KeybindManager::ResetKeybind(const std::string& command_id)
    {
        auto it = commands_.find(command_id);
        if (it != commands_.end())
        {
            it->second.current_keybind = it->second.definition.default_keybind;
        }
    }

    void KeybindManager::ResetAllKeybinds()
    {
        for (auto& [id, binding] : commands_)
        {
            binding.current_keybind = binding.definition.default_keybind;
        }
        SPDLOG_INFO("Reset all keybinds to defaults");
    }

    std::optional<std::string> KeybindManager::GetCommandForKeybind(const Keybind& keybind) const
    {
        for (const auto& [id, binding] : commands_)
        {
            if (binding.current_keybind == keybind)
                return id;
        }
        return std::nullopt;
    }

    bool KeybindManager::HasConflict(const Keybind& keybind, const std::string& exclude_command) const
    {
        for (const auto& [id, binding] : commands_)
        {
            if (id == exclude_command)
                continue;

            if (binding.current_keybind == keybind)
                return true;
        }
        return false;
    }

    bool KeybindManager::ProcessInput()
    {
        if (!enabled_)
            return false;

        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput)
            return false;

        for (const auto& [id, binding] : commands_)
        {
            if (binding.current_keybind.IsPressed())
            {
                return ExecuteCommand(id);
            }
        }

        return false;
    }

    std::vector<CommandDef> KeybindManager::GetAllCommands() const
    {
        std::vector<CommandDef> result;
        result.reserve(commands_.size());

        for (const auto& [id, binding] : commands_)
        {
            result.push_back(binding.definition);
        }

        std::sort(result.begin(), result.end(),
            [](const CommandDef& a, const CommandDef& b) {
                if (a.category != b.category)
                    return static_cast<int>(a.category) < static_cast<int>(b.category);
                return a.name < b.name;
            });

        return result;
    }

    std::vector<CommandDef> KeybindManager::GetCommandsByCategory(CommandCategory category) const
    {
        std::vector<CommandDef> result;

        for (const auto& [id, binding] : commands_)
        {
            if (binding.definition.category == category)
                result.push_back(binding.definition);
        }

        std::sort(result.begin(), result.end(),
            [](const CommandDef& a, const CommandDef& b) { return a.name < b.name; });

        return result;
    }

    std::vector<CommandDef> KeybindManager::SearchCommands(const std::string& query) const
    {
        std::vector<CommandDef> result;
        std::string lower_query = query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

        for (const auto& [id, binding] : commands_)
        {
            std::string lower_name = binding.definition.name;
            std::string lower_desc = binding.definition.description;
            std::string lower_id = id;

            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            std::transform(lower_desc.begin(), lower_desc.end(), lower_desc.begin(), ::tolower);
            std::transform(lower_id.begin(), lower_id.end(), lower_id.begin(), ::tolower);

            if (lower_name.find(lower_query) != std::string::npos ||
                lower_desc.find(lower_query) != std::string::npos ||
                lower_id.find(lower_query) != std::string::npos)
            {
                result.push_back(binding.definition);
            }
        }

        return result;
    }

    std::string KeybindManager::GetShortcutHelpText(const std::string& command_id) const
    {
        auto it = commands_.find(command_id);
        if (it == commands_.end())
            return "";

        return it->second.current_keybind.ToString();
    }

    bool KeybindManager::LoadKeybinds(const std::string& path)
    {
        // TODO: Implement JSON loading
        SPDLOG_INFO("Loading keybinds from: {}", path);
        return false;
    }

    bool KeybindManager::SaveKeybinds(const std::string& path) const
    {
        // TODO: Implement JSON saving
        SPDLOG_INFO("Saving keybinds to: {}", path);
        return false;
    }

    bool KeybindManager::ExportKeybinds(const std::string& path) const
    {
        return SaveKeybinds(path);
    }

    bool KeybindManager::ImportKeybinds(const std::string& path)
    {
        return LoadKeybinds(path);
    }

    void KeybindManager::RenderSettingsUI()
    {
        static int selected_category = 0;
        const char* categories[] = {
            "Navigation", "File Operations", "Selection", "View", "Tabs", "Panes", "Search", "Help"
        };

        ImGui::Text("Keyboard Shortcuts");
        ImGui::Separator();

        // Category selector
        ImGui::Combo("Category", &selected_category, categories, IM_ARRAYSIZE(categories));
        ImGui::Separator();

        // Commands list
        auto commands = GetCommandsByCategory(static_cast<CommandCategory>(selected_category));

        if (ImGui::BeginTable("KeybindTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (const auto& cmd : commands)
            {
                ImGui::TableNextRow();
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", cmd.name.c_str());
                if (ImGui::IsItemHovered() && !cmd.description.empty())
                {
                    ImGui::SetTooltip("%s", cmd.description.c_str());
                }

                ImGui::TableNextColumn();
                auto keybind = GetKeybind(cmd.id);
                std::string shortcut = keybind ? keybind->ToString() : "(none)";
                
                if (listening_for_keybind_ && keybind_target_command_ == cmd.id)
                {
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Press keys...");
                    
                    // Listen for key press
                    for (int i = static_cast<int>(ImGuiKey_NamedKey_BEGIN); 
                         i < static_cast<int>(ImGuiKey_NamedKey_END); ++i)
                    {
                        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(i)))
                        {
                            Keybind new_bind;
                            new_bind.key = i;
                            
                            ImGuiIO& io = ImGui::GetIO();
                            if (io.KeyCtrl) new_bind.modifiers = new_bind.modifiers | ModifierKeys::Ctrl;
                            if (io.KeyShift) new_bind.modifiers = new_bind.modifiers | ModifierKeys::Shift;
                            if (io.KeyAlt) new_bind.modifiers = new_bind.modifiers | ModifierKeys::Alt;

                            SetKeybind(cmd.id, new_bind);
                            listening_for_keybind_ = false;
                            keybind_target_command_.clear();
                            break;
                        }
                    }

                    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    {
                        listening_for_keybind_ = false;
                        keybind_target_command_.clear();
                    }
                }
                else
                {
                    ImGui::Text("%s", shortcut.c_str());
                }

                ImGui::TableNextColumn();
                opacity::ui::ImGuiScopedID scoped_id_cmd(cmd.id.c_str());
                
                if (cmd.allow_rebind)
                {
                    if (ImGui::SmallButton("Edit"))
                    {
                        listening_for_keybind_ = true;
                        keybind_target_command_ = cmd.id;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset"))
                    {
                        ResetKeybind(cmd.id);
                    }
                }
                
                // RAII will pop ID
            }

            ImGui::EndTable();
        }

        ImGui::Separator();
        if (ImGui::Button("Reset All to Defaults"))
        {
            ResetAllKeybinds();
        }
    }

    bool KeybindManager::RenderCommandPalette()
    {
        if (!show_command_palette_)
            return false;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_AlwaysAutoResize;

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(ImVec2(center.x, center.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 0));

        if (ImGui::Begin("##CommandPalette", &show_command_palette_, flags))
        {
            // Search input
            ImGui::SetNextItemWidth(-1);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            
            if (ImGui::InputText("##Search", palette_search_, sizeof(palette_search_),
                ImGuiInputTextFlags_EnterReturnsTrue))
            {
                // Execute first matching command
                auto matches = SearchCommands(palette_search_);
                if (!matches.empty())
                {
                    ExecuteCommand(matches[0].id);
                    show_command_palette_ = false;
                    palette_search_[0] = '\0';
                }
            }

            // Escape to close
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                show_command_palette_ = false;
                palette_search_[0] = '\0';
            }

            ImGui::Separator();

            // Results list
            auto matches = strlen(palette_search_) > 0 
                ? SearchCommands(palette_search_) 
                : GetAllCommands();

            ImGui::BeginChild("##Results", ImVec2(0, 300), false);

            for (size_t i = 0; i < matches.size() && i < 20; ++i)
            {
                const auto& cmd = matches[i];
                auto keybind = GetKeybind(cmd.id);

                opacity::ui::ImGuiScopedID scoped_id_i(static_cast<int>(i));
                
                if (ImGui::Selectable("##cmd", false, ImGuiSelectableFlags_None, ImVec2(0, 24)))
                {
                    ExecuteCommand(cmd.id);
                    show_command_palette_ = false;
                    palette_search_[0] = '\0';
                }

                ImGui::SameLine();
                ImGui::Text("%s", cmd.name.c_str());

                if (keybind && keybind->IsValid())
                {
                    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
                    ImGui::TextDisabled("%s", keybind->ToString().c_str());
                }

                // RAII handles PopID
            }

            ImGui::EndChild();
        }
        ImGui::End();

        return show_command_palette_;
    }

} // namespace opacity::ui
