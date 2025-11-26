#include "opacity/ui/CommandPalette.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "imgui.h"
#include "opacity/ui/ImGuiScoped.h"

namespace opacity::ui
{
    CommandPalette::CommandPalette() = default;
    CommandPalette::~CommandPalette() = default;

    void CommandPalette::RegisterCommand(const PaletteCommand& command)
    {
        commands_[command.id] = command;
        SPDLOG_DEBUG("Registered command: {}", command.id);
    }

    void CommandPalette::RegisterCommands(const std::vector<PaletteCommand>& commands)
    {
        for (const auto& cmd : commands)
        {
            RegisterCommand(cmd);
        }
    }

    void CommandPalette::UnregisterCommand(const std::string& id)
    {
        commands_.erase(id);
    }

    void CommandPalette::ClearCommands()
    {
        commands_.clear();
    }

    const PaletteCommand* CommandPalette::GetCommand(const std::string& id) const
    {
        auto it = commands_.find(id);
        if (it != commands_.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    std::vector<const PaletteCommand*> CommandPalette::GetAllCommands() const
    {
        std::vector<const PaletteCommand*> result;
        result.reserve(commands_.size());
        
        for (const auto& [id, cmd] : commands_)
        {
            result.push_back(&cmd);
        }

        std::sort(result.begin(), result.end(),
            [](const PaletteCommand* a, const PaletteCommand* b)
            {
                if (a->category != b->category)
                    return a->category < b->category;
                if (a->priority != b->priority)
                    return a->priority > b->priority;
                return a->label < b->label;
            });

        return result;
    }

    std::vector<const PaletteCommand*> CommandPalette::GetCommandsByCategory(
        const std::string& category) const
    {
        std::vector<const PaletteCommand*> result;
        
        for (const auto& [id, cmd] : commands_)
        {
            if (cmd.category == category)
            {
                result.push_back(&cmd);
            }
        }

        std::sort(result.begin(), result.end(),
            [](const PaletteCommand* a, const PaletteCommand* b)
            {
                if (a->priority != b->priority)
                    return a->priority > b->priority;
                return a->label < b->label;
            });

        return result;
    }

    std::vector<std::string> CommandPalette::GetCategories() const
    {
        std::set<std::string> categories;
        for (const auto& [id, cmd] : commands_)
        {
            if (!cmd.category.empty())
            {
                categories.insert(cmd.category);
            }
        }
        return std::vector<std::string>(categories.begin(), categories.end());
    }

    std::vector<PaletteMatch> CommandPalette::Search(const std::string& query, 
                                                       size_t max_results) const
    {
        std::vector<PaletteMatch> results;

        if (query.empty())
        {
            // Return recent commands when no query
            if (boost_recent_)
            {
                auto recent = GetRecentCommands(max_results);
                for (const auto* cmd : recent)
                {
                    PaletteMatch match;
                    match.command = cmd;
                    match.score = 1000; // High score for recent
                    results.push_back(match);
                }
            }
            return results;
        }

        std::string lower_query = query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

        for (const auto& [id, cmd] : commands_)
        {
            if (!cmd.enabled) continue;

            // Filter by category if set
            if (!category_filter_.empty() && cmd.category != category_filter_)
            {
                continue;
            }

            PaletteMatch match;
            match.command = &cmd;

            // Calculate score from label
            std::string lower_label = cmd.label;
            std::transform(lower_label.begin(), lower_label.end(), lower_label.begin(), ::tolower);
            match.score = CalculateFuzzyScore(lower_label, lower_query, match.matched_indices);

            // Boost exact prefix matches
            if (lower_label.find(lower_query) == 0)
            {
                match.score += 100;
            }
            // Boost word boundary matches
            else if (lower_label.find(" " + lower_query) != std::string::npos)
            {
                match.score += 50;
            }

            // Check description and keywords for additional matches
            if (match.score == 0 && !cmd.description.empty())
            {
                std::string lower_desc = cmd.description;
                std::transform(lower_desc.begin(), lower_desc.end(), lower_desc.begin(), ::tolower);
                std::vector<size_t> desc_indices;
                int desc_score = CalculateFuzzyScore(lower_desc, lower_query, desc_indices);
                if (desc_score > 0)
                {
                    match.score = desc_score / 2; // Lower weight for description matches
                }
            }

            if (match.score == 0 && !cmd.keywords.empty())
            {
                for (const auto& keyword : cmd.keywords)
                {
                    std::string lower_kw = keyword;
                    std::transform(lower_kw.begin(), lower_kw.end(), lower_kw.begin(), ::tolower);
                    if (lower_kw.find(lower_query) != std::string::npos)
                    {
                        match.score = 30;
                        break;
                    }
                }
            }

            // Boost by priority
            match.score += cmd.priority;

            // Boost recent commands
            if (boost_recent_)
            {
                for (const auto& entry : history_)
                {
                    if (entry.command_id == cmd.id)
                    {
                        match.score += 20 + entry.use_count;
                        break;
                    }
                }
            }

            if (match.score > 0)
            {
                results.push_back(match);
            }
        }

        // Sort by score (descending)
        std::sort(results.begin(), results.end(),
            [](const PaletteMatch& a, const PaletteMatch& b)
            {
                return a.score > b.score;
            });

        // Limit results
        if (max_results > 0 && results.size() > max_results)
        {
            results.resize(max_results);
        }

        return results;
    }

    bool CommandPalette::Execute(const std::string& id)
    {
        auto* cmd = GetCommand(id);
        if (cmd && cmd->enabled && cmd->action)
        {
            SPDLOG_INFO("Executing command: {}", id);
            AddToHistory(id);
            cmd->action();
            return true;
        }
        return false;
    }

    void CommandPalette::SetCommandEnabled(const std::string& id, bool enabled)
    {
        auto it = commands_.find(id);
        if (it != commands_.end())
        {
            it->second.enabled = enabled;
        }
    }

    std::vector<const PaletteCommand*> CommandPalette::GetRecentCommands(size_t max_count) const
    {
        std::vector<const PaletteCommand*> result;

        for (const auto& entry : history_)
        {
            if (result.size() >= max_count) break;

            auto* cmd = GetCommand(entry.command_id);
            if (cmd && cmd->enabled)
            {
                result.push_back(cmd);
            }
        }

        return result;
    }

    void CommandPalette::ClearHistory()
    {
        history_.clear();
    }

    void CommandPalette::Show()
    {
        visible_ = true;
        current_query_.clear();
        input_buffer_[0] = '\0';
        selected_index_ = 0;
        UpdateResults();
    }

    void CommandPalette::Hide()
    {
        visible_ = false;
    }

    void CommandPalette::Toggle()
    {
        if (visible_)
            Hide();
        else
            Show();
    }

    void CommandPalette::SetQuery(const std::string& query)
    {
        if (current_query_ != query)
        {
            current_query_ = query;
            selected_index_ = 0;
            UpdateResults();
        }
    }

    void CommandPalette::SetSelectedIndex(int index)
    {
        if (!current_results_.empty())
        {
            selected_index_ = std::clamp(index, 0, static_cast<int>(current_results_.size()) - 1);
        }
        else
        {
            selected_index_ = 0;
        }
    }

    void CommandPalette::SelectPrevious()
    {
        if (selected_index_ > 0)
        {
            --selected_index_;
        }
    }

    void CommandPalette::SelectNext()
    {
        if (!current_results_.empty() && 
            selected_index_ < static_cast<int>(current_results_.size()) - 1)
        {
            ++selected_index_;
        }
    }

    bool CommandPalette::ExecuteSelected()
    {
        if (!current_results_.empty() && 
            selected_index_ >= 0 && 
            selected_index_ < static_cast<int>(current_results_.size()))
        {
            const auto& match = current_results_[selected_index_];
            if (match.command)
            {
                Hide();
                return Execute(match.command->id);
            }
        }
        return false;
    }

    void CommandPalette::SetCategoryFilter(const std::string& category)
    {
        if (category_filter_ != category)
        {
            category_filter_ = category;
            UpdateResults();
        }
    }

    bool CommandPalette::Render()
    {
        if (!visible_)
        {
            return false;
        }

        // Center the palette at the top of the screen
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 window_size(500, 400);
        ImVec2 window_pos((io.DisplaySize.x - window_size.x) / 2, 50);

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse;

        if (ImGui::Begin("##CommandPalette", nullptr, flags))
        {
            // Search input
            ImGui::SetNextItemWidth(-1);
            
            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere();
            }

            bool input_changed = ImGui::InputText("##PaletteInput", input_buffer_, 
                sizeof(input_buffer_), ImGuiInputTextFlags_EnterReturnsTrue);

            // Handle Enter key
            if (input_changed || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)))
            {
                if (ExecuteSelected())
                {
                    ImGui::End();
                    return false;
                }
            }

            // Handle Escape
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                Hide();
                ImGui::End();
                return false;
            }

            // Handle Up/Down arrows
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                SelectPrevious();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                SelectNext();
            }

            // Update query if changed
            std::string new_query = input_buffer_;
            if (new_query != current_query_)
            {
                SetQuery(new_query);
            }

            ImGui::Separator();

            // Results list
            ImGui::BeginChild("##Results", ImVec2(0, 0), false);

            for (size_t i = 0; i < current_results_.size(); ++i)
            {
                const auto& match = current_results_[i];
                if (!match.command) continue;

                bool is_selected = (static_cast<int>(i) == selected_index_);

                opacity::ui::ImGuiScopedID scoped_id(static_cast<int>(i));

                // Selectable for the entire row
                if (ImGui::Selectable("##item", is_selected, 
                    ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 40)))
                {
                    selected_index_ = static_cast<int>(i);
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        ExecuteSelected();
                        ImGui::EndChild();
                        ImGui::End();
                        return false;
                    }
                }

                ImGui::SameLine(5);
                opacity::ui::ImGuiScopedGroup scoped_group;

                // Category badge
                if (!match.command->category.empty())
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), 
                        "[%s]", match.command->category.c_str());
                    ImGui::SameLine();
                }

                // Command label with highlighted matches
                ImGui::TextUnformatted(match.command->label.c_str());

                // Shortcut on the right
                if (show_shortcuts_ && !match.command->shortcut.empty())
                {
                    ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), 
                        "%s", match.command->shortcut.c_str());
                }

                // Description below
                if (show_descriptions_ && !match.command->description.empty())
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), 
                        "%s", match.command->description.c_str());
                }

                // RAII will end the group and pop the id
            }

            if (current_results_.empty())
            {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No matching commands");
            }

            ImGui::EndChild();
        }
        ImGui::End();

        return visible_;
    }

    int CommandPalette::CalculateFuzzyScore(const std::string& text, const std::string& query,
                                             std::vector<size_t>& matched_indices) const
    {
        if (query.empty()) return 0;
        if (text.empty()) return 0;

        matched_indices.clear();
        int score = 0;
        size_t query_idx = 0;
        size_t prev_match_idx = std::string::npos;
        bool prev_was_match = false;

        for (size_t i = 0; i < text.size() && query_idx < query.size(); ++i)
        {
            if (std::tolower(text[i]) == std::tolower(query[query_idx]))
            {
                matched_indices.push_back(i);

                // Base score for match
                score += 10;

                // Bonus for consecutive matches
                if (prev_was_match && prev_match_idx + 1 == i)
                {
                    score += 15;
                }

                // Bonus for matching at word boundary
                if (i == 0 || !std::isalnum(text[i - 1]))
                {
                    score += 20;
                }

                // Bonus for matching uppercase in camelCase
                if (std::isupper(text[i]))
                {
                    score += 5;
                }

                prev_match_idx = i;
                prev_was_match = true;
                ++query_idx;
            }
            else
            {
                prev_was_match = false;
            }
        }

        // Only return score if all query characters matched
        if (query_idx < query.size())
        {
            matched_indices.clear();
            return 0;
        }

        // Bonus for shorter strings (more specific matches)
        score += std::max(0, 50 - static_cast<int>(text.size()));

        return score;
    }

    void CommandPalette::AddToHistory(const std::string& command_id)
    {
        // Check if already in history
        for (auto& entry : history_)
        {
            if (entry.command_id == command_id)
            {
                ++entry.use_count;
                entry.timestamp = std::chrono::system_clock::now();
                
                // Move to front
                std::rotate(history_.begin(), 
                           std::find_if(history_.begin(), history_.end(),
                               [&](const PaletteHistoryEntry& e) { 
                                   return e.command_id == command_id; 
                               }),
                           history_.end());
                return;
            }
        }

        // Add new entry
        PaletteHistoryEntry entry;
        entry.command_id = command_id;
        entry.timestamp = std::chrono::system_clock::now();
        entry.use_count = 1;

        history_.insert(history_.begin(), entry);

        // Trim history
        while (history_.size() > max_history_size_)
        {
            history_.pop_back();
        }
    }

    void CommandPalette::UpdateResults()
    {
        current_results_ = Search(current_query_, 20);
        selected_index_ = 0;
    }

    void RegisterStandardCommands(CommandPalette& palette)
    {
        std::vector<PaletteCommand> commands = {
            // File Operations
            {"file.new_folder", "New Folder", "Create a new folder", "File", "Ctrl+Shift+N", nullptr, true, 10, {"create", "mkdir"}},
            {"file.new_file", "New File", "Create a new file", "File", "Ctrl+N", nullptr, true, 10, {"create"}},
            {"file.open", "Open", "Open selected file", "File", "Enter", nullptr, true, 10, {}},
            {"file.rename", "Rename", "Rename selected item", "File", "F2", nullptr, true, 10, {}},
            {"file.delete", "Delete", "Delete selected items", "File", "Delete", nullptr, true, 10, {"remove"}},
            {"file.copy", "Copy", "Copy selected items", "File", "Ctrl+C", nullptr, true, 10, {}},
            {"file.cut", "Cut", "Cut selected items", "File", "Ctrl+X", nullptr, true, 10, {}},
            {"file.paste", "Paste", "Paste items", "File", "Ctrl+V", nullptr, true, 10, {}},
            {"file.properties", "Properties", "Show file properties", "File", "Alt+Enter", nullptr, true, 5, {"info"}},

            // Navigation
            {"nav.back", "Go Back", "Navigate to previous folder", "Navigation", "Alt+Left", nullptr, true, 10, {"previous"}},
            {"nav.forward", "Go Forward", "Navigate to next folder", "Navigation", "Alt+Right", nullptr, true, 10, {"next"}},
            {"nav.up", "Go Up", "Navigate to parent folder", "Navigation", "Alt+Up", nullptr, true, 10, {"parent"}},
            {"nav.home", "Go Home", "Navigate to home folder", "Navigation", "Alt+Home", nullptr, true, 10, {"user"}},
            {"nav.refresh", "Refresh", "Refresh current view", "Navigation", "F5", nullptr, true, 10, {"reload"}},

            // Selection
            {"select.all", "Select All", "Select all items", "Selection", "Ctrl+A", nullptr, true, 10, {}},
            {"select.none", "Deselect All", "Clear selection", "Selection", "Ctrl+D", nullptr, true, 10, {"clear"}},
            {"select.invert", "Invert Selection", "Invert current selection", "Selection", "Ctrl+I", nullptr, true, 10, {}},

            // View
            {"view.details", "Details View", "Show details view", "View", "", nullptr, true, 5, {"list"}},
            {"view.icons", "Icons View", "Show icons view", "View", "", nullptr, true, 5, {"grid"}},
            {"view.hidden", "Toggle Hidden Files", "Show/hide hidden files", "View", "Ctrl+H", nullptr, true, 10, {"dotfiles"}},
            {"view.preview", "Toggle Preview Panel", "Show/hide preview panel", "View", "Alt+P", nullptr, true, 10, {}},
            {"view.dual_pane", "Toggle Dual Pane", "Switch between single and dual pane", "View", "F3", nullptr, true, 10, {"split"}},

            // Search
            {"search.find", "Find", "Search in current folder", "Search", "Ctrl+F", nullptr, true, 10, {"search"}},
            {"search.advanced", "Advanced Search", "Open advanced search dialog", "Search", "Ctrl+Shift+F", nullptr, true, 10, {}},
            {"search.duplicates", "Find Duplicates", "Find duplicate files", "Search", "", nullptr, true, 10, {}},

            // Tools
            {"tools.compare_files", "Compare Files", "Compare two files", "Tools", "", nullptr, true, 10, {"diff"}},
            {"tools.compare_folders", "Compare Folders", "Compare two folders", "Tools", "", nullptr, true, 10, {"diff"}},
            {"tools.batch_rename", "Batch Rename", "Rename multiple files", "Tools", "", nullptr, true, 10, {}},
            {"tools.archive_create", "Create Archive", "Create ZIP archive from selection", "Tools", "", nullptr, true, 10, {"zip", "compress"}},
            {"tools.archive_extract", "Extract Archive", "Extract archive contents", "Tools", "", nullptr, true, 10, {"unzip", "decompress"}},

            // Settings
            {"settings.preferences", "Preferences", "Open settings", "Settings", "Ctrl+,", nullptr, true, 10, {"options", "config"}},
            {"settings.keybinds", "Keyboard Shortcuts", "Configure keyboard shortcuts", "Settings", "", nullptr, true, 10, {"hotkeys"}},
            {"settings.theme", "Theme", "Change color theme", "Settings", "", nullptr, true, 10, {"colors", "appearance"}},

            // Help
            {"help.about", "About", "About Opacity", "Help", "", nullptr, true, 5, {}},
            {"help.shortcuts", "Keyboard Reference", "Show keyboard shortcuts", "Help", "F1", nullptr, true, 10, {"hotkeys"}},
        };

        palette.RegisterCommands(commands);
    }

} // namespace opacity::ui
