#include "opacity/ui/AdvancedSearchDialog.h"
#include "opacity/core/Logger.h"

#include <imgui.h>
#include <algorithm>
#include <sstream>

namespace opacity::ui
{
    AdvancedSearchDialog::AdvancedSearchDialog() = default;
    AdvancedSearchDialog::~AdvancedSearchDialog() = default;

    void AdvancedSearchDialog::Show()
    {
        visible_ = true;
    }

    void AdvancedSearchDialog::Hide()
    {
        visible_ = false;
    }

    void AdvancedSearchDialog::SetSearchPath(const std::string& path)
    {
        criteria_.search_path = path;
        strncpy(path_buffer_, path.c_str(), sizeof(path_buffer_) - 1);
        path_buffer_[sizeof(path_buffer_) - 1] = '\0';
    }

    bool AdvancedSearchDialog::Render()
    {
        if (!visible_)
            return false;

        bool search_started = false;

        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Advanced Search", &visible_, ImGuiWindowFlags_NoCollapse))
        {
            // Tab bar
            if (ImGui::BeginTabBar("SearchTabs"))
            {
                if (ImGui::BeginTabItem("Basic"))
                {
                    current_tab_ = 0;
                    RenderBasicTab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Filters"))
                {
                    current_tab_ = 1;
                    RenderFiltersTab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Saved Searches"))
                {
                    current_tab_ = 2;
                    RenderSavedSearchesTab();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::Separator();

            // Bottom buttons
            float button_width = 120.0f;
            float total_width = button_width * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
            float start_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;

            ImGui::SetCursorPosX(start_x);

            if (ImGui::Button("Search", ImVec2(button_width, 0)))
            {
                // Update criteria from UI
                criteria_.name_pattern = name_buffer_;
                criteria_.content_pattern = content_buffer_;
                criteria_.search_path = path_buffer_;

                if (on_search_start_)
                {
                    on_search_start_(criteria_);
                    search_started = true;
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Reset", ImVec2(button_width, 0)))
            {
                ResetCriteria();
            }

            ImGui::SameLine();

            if (ImGui::Button("Close", ImVec2(button_width, 0)))
            {
                Hide();
            }
        }
        ImGui::End();

        return search_started;
    }

    void AdvancedSearchDialog::RenderBasicTab()
    {
        // Search path
        ImGui::Text("Search in:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##Path", path_buffer_, sizeof(path_buffer_));

        ImGui::Spacing();

        // Name pattern
        ImGui::Text("File name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##Name", name_buffer_, sizeof(name_buffer_));

        ImGui::Checkbox("Case sensitive", &criteria_.name_case_sensitive);
        ImGui::SameLine();
        ImGui::Checkbox("Use regex", &criteria_.name_use_regex);
        ImGui::SameLine();
        ImGui::Checkbox("Whole word", &criteria_.name_whole_word);

        ImGui::Spacing();

        // Content search
        ImGui::Checkbox("Search file contents", &criteria_.search_contents);
        
        if (criteria_.search_contents)
        {
            ImGui::Text("Content pattern:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Content", content_buffer_, sizeof(content_buffer_));

            ImGui::Checkbox("Case sensitive##Content", &criteria_.content_case_sensitive);
            ImGui::SameLine();
            ImGui::Checkbox("Use regex##Content", &criteria_.content_use_regex);
        }

        ImGui::Spacing();

        // Scope options
        ImGui::Text("Scope:");
        ImGui::Checkbox("Include subdirectories", &criteria_.recursive);
        
        if (criteria_.recursive)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputInt("Max depth", &criteria_.max_depth);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("-1 = unlimited");
        }
    }

    void AdvancedSearchDialog::RenderFiltersTab()
    {
        // File type section
        if (ImGui::CollapsingHeader("File Types", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Include files", &criteria_.include_files);
            ImGui::SameLine();
            ImGui::Checkbox("Include directories", &criteria_.include_directories);

            ImGui::Checkbox("Include hidden", &criteria_.include_hidden);
            ImGui::SameLine();
            ImGui::Checkbox("Include system", &criteria_.include_system);

            ImGui::Spacing();

            ImGui::Text("Include extensions (comma-separated):");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##IncludeExt", extensions_include_, sizeof(extensions_include_));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("e.g., .txt, .cpp, .h");

            ImGui::Text("Exclude extensions (comma-separated):");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ExcludeExt", extensions_exclude_, sizeof(extensions_exclude_));
        }

        // Size section
        if (ImGui::CollapsingHeader("Size", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* size_options[] = { "Any", "Less than", "Greater than", "Between", "Equals" };
            int size_comp = static_cast<int>(criteria_.size_comparison);
            
            ImGui::SetNextItemWidth(150);
            if (ImGui::Combo("Size filter", &size_comp, size_options, IM_ARRAYSIZE(size_options)))
            {
                criteria_.size_comparison = static_cast<SizeComparison>(size_comp);
            }

            if (criteria_.size_comparison != SizeComparison::Any)
            {
                const char* unit_options[] = { "Bytes", "KB", "MB", "GB" };
                int unit = static_cast<int>(criteria_.size_unit);

                if (criteria_.size_comparison == SizeComparison::Between)
                {
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputInt("Min##Size", &size_min_input_);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputInt("Max##Size", &size_max_input_);
                }
                else
                {
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputInt("Size##Value", &size_min_input_);
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);
                if (ImGui::Combo("##Unit", &unit, unit_options, IM_ARRAYSIZE(unit_options)))
                {
                    criteria_.size_unit = static_cast<SizeUnit>(unit);
                }

                criteria_.size_min = GetSizeInBytes(static_cast<uint64_t>(size_min_input_), criteria_.size_unit);
                criteria_.size_max = GetSizeInBytes(static_cast<uint64_t>(size_max_input_), criteria_.size_unit);
            }
        }

        // Date section
        if (ImGui::CollapsingHeader("Date Modified", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* date_options[] = { 
                "Any", "Before", "After", "Between", 
                "Today", "Yesterday", "This Week", "This Month", "This Year" 
            };
            int date_comp = static_cast<int>(criteria_.date_comparison);
            
            ImGui::SetNextItemWidth(150);
            if (ImGui::Combo("Date filter", &date_comp, date_options, IM_ARRAYSIZE(date_options)))
            {
                criteria_.date_comparison = static_cast<DateComparison>(date_comp);
            }

            // TODO: Add date picker widgets for Before/After/Between options
        }

        // Attributes section
        if (ImGui::CollapsingHeader("Attributes"))
        {
            ImGui::Checkbox("Read-only", &criteria_.filter_readonly);
            ImGui::SameLine();
            ImGui::Checkbox("Archive", &criteria_.filter_archive);
            
            ImGui::Checkbox("Compressed", &criteria_.filter_compressed);
            ImGui::SameLine();
            ImGui::Checkbox("Encrypted", &criteria_.filter_encrypted);
        }
    }

    void AdvancedSearchDialog::RenderSavedSearchesTab()
    {
        // List of saved searches
        ImGui::BeginChild("SavedSearchList", ImVec2(0, -60), true);

        for (size_t i = 0; i < saved_searches_.size(); ++i)
        {
            const auto& search = saved_searches_[i];
            
            ImGui::PushID(static_cast<int>(i));
            
            if (ImGui::Selectable(search.name.c_str()))
            {
                LoadSavedSearch(search.name);
            }

            if (ImGui::IsItemHovered() && !search.description.empty())
            {
                ImGui::SetTooltip("%s", search.description.c_str());
            }

            // Context menu
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Load"))
                {
                    LoadSavedSearch(search.name);
                }
                if (ImGui::MenuItem("Delete"))
                {
                    DeleteSavedSearch(search.name);
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();

        // Save current search
        static char save_name[128] = "";
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##SaveName", save_name, sizeof(save_name));
        ImGui::SameLine();
        
        if (ImGui::Button("Save Current"))
        {
            if (strlen(save_name) > 0)
            {
                SaveCurrentSearch(save_name);
                save_name[0] = '\0';
            }
        }
    }

    void AdvancedSearchDialog::RenderResultsPanel()
    {
        // This would be rendered in a separate window/panel
        // Showing search progress and results
    }

    void AdvancedSearchDialog::ResetCriteria()
    {
        criteria_ = AdvancedSearchCriteria();
        
        name_buffer_[0] = '\0';
        content_buffer_[0] = '\0';
        extensions_include_[0] = '\0';
        extensions_exclude_[0] = '\0';
        size_min_input_ = 0;
        size_max_input_ = 0;

        SPDLOG_DEBUG("Search criteria reset");
    }

    uint64_t AdvancedSearchDialog::GetSizeInBytes(uint64_t value, SizeUnit unit) const
    {
        switch (unit)
        {
        case SizeUnit::Bytes: return value;
        case SizeUnit::KB: return value * 1024ULL;
        case SizeUnit::MB: return value * 1024ULL * 1024ULL;
        case SizeUnit::GB: return value * 1024ULL * 1024ULL * 1024ULL;
        default: return value;
        }
    }

    void AdvancedSearchDialog::SaveCurrentSearch(const std::string& name)
    {
        // Check if name already exists
        auto it = std::find_if(saved_searches_.begin(), saved_searches_.end(),
            [&name](const SavedSearch& s) { return s.name == name; });

        SavedSearch search;
        search.name = name;
        search.criteria = criteria_;
        search.last_used = std::chrono::system_clock::now();

        if (it != saved_searches_.end())
        {
            *it = std::move(search);
        }
        else
        {
            saved_searches_.push_back(std::move(search));
        }

        SPDLOG_INFO("Saved search: {}", name);
    }

    void AdvancedSearchDialog::LoadSavedSearch(const std::string& name)
    {
        auto it = std::find_if(saved_searches_.begin(), saved_searches_.end(),
            [&name](const SavedSearch& s) { return s.name == name; });

        if (it != saved_searches_.end())
        {
            criteria_ = it->criteria;
            it->last_used = std::chrono::system_clock::now();

            // Update UI buffers
            strncpy(name_buffer_, criteria_.name_pattern.c_str(), sizeof(name_buffer_) - 1);
            strncpy(content_buffer_, criteria_.content_pattern.c_str(), sizeof(content_buffer_) - 1);
            strncpy(path_buffer_, criteria_.search_path.c_str(), sizeof(path_buffer_) - 1);

            SPDLOG_INFO("Loaded search: {}", name);
        }
    }

    void AdvancedSearchDialog::DeleteSavedSearch(const std::string& name)
    {
        auto it = std::find_if(saved_searches_.begin(), saved_searches_.end(),
            [&name](const SavedSearch& s) { return s.name == name; });

        if (it != saved_searches_.end())
        {
            saved_searches_.erase(it);
            SPDLOG_INFO("Deleted search: {}", name);
        }
    }

    std::vector<SavedSearch> AdvancedSearchDialog::GetSavedSearches() const
    {
        return saved_searches_;
    }

    bool AdvancedSearchDialog::LoadSearches(const std::string& path)
    {
        // TODO: Implement JSON loading
        SPDLOG_INFO("Loading saved searches from: {}", path);
        return false;
    }

    bool AdvancedSearchDialog::SaveSearches(const std::string& path) const
    {
        // TODO: Implement JSON saving
        SPDLOG_INFO("Saving searches to: {}", path);
        return false;
    }

} // namespace opacity::ui
