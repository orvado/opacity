#include "opacity/ui/DiffViewer.h"
#include "opacity/core/Logger.h"

#include <imgui.h>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace opacity::ui
{
    using diff::DiffType;
    
    DiffViewer::DiffViewer() = default;
    DiffViewer::~DiffViewer() = default;

    void DiffViewer::Show()
    {
        visible_ = true;
    }

    void DiffViewer::Hide()
    {
        visible_ = false;
    }

    void DiffViewer::LoadDiff(const diff::DiffResult& result)
    {
        result_ = result;
        current_hunk_ = 0;
        scroll_y_ = 0.0f;
        
        left_path_ = result.left_file;
        right_path_ = result.right_file;
        
        SPDLOG_INFO("Loaded diff: {} vs {}, {} hunks", 
            result.left_file, result.right_file, result.hunks.size());
    }

    void DiffViewer::CompareFiles(const std::string& left_path, const std::string& right_path)
    {
        left_path_ = left_path;
        right_path_ = right_path;
        
        auto result = engine_.CompareFiles(core::Path(left_path), core::Path(right_path), diff_options_);
        LoadDiff(result);
        
        Show();
    }

    void DiffViewer::CompareText(const std::string& left_text, const std::string& right_text,
                                 const std::string& left_name, const std::string& right_name)
    {
        left_path_ = left_name;
        right_path_ = right_name;
        
        auto result = engine_.CompareText(left_text, right_text, diff_options_);
        result.left_file = left_name;
        result.right_file = right_name;
        LoadDiff(result);
        
        Show();
    }

    void DiffViewer::NextDifference()
    {
        if (current_hunk_ + 1 < result_.hunks.size())
        {
            ++current_hunk_;
        }
    }

    void DiffViewer::PreviousDifference()
    {
        if (current_hunk_ > 0)
        {
            --current_hunk_;
        }
    }

    void DiffViewer::GoToHunk(size_t index)
    {
        if (index < result_.hunks.size())
        {
            current_hunk_ = index;
        }
    }

    size_t DiffViewer::GetHunkCount() const
    {
        return result_.hunks.size();
    }

    void DiffViewer::Render()
    {
        if (!visible_)
            return;

        ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
        
        std::string title = "Diff: " + left_path_ + " <-> " + right_path_;
        
        if (ImGui::Begin(title.c_str(), &visible_, ImGuiWindowFlags_MenuBar))
        {
            RenderToolbar();
            
            if (!result_.success)
            {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Error: %s", result_.error_message.c_str());
            }
            else if (result_.AreIdentical())
            {
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1), "Files are identical");
            }
            else
            {
                // Statistics
                ImGui::Text("Changes: +%zu -%zu ~%zu", 
                    result_.lines_added, result_.lines_removed, result_.lines_modified);
                ImGui::SameLine();
                RenderHunkNavigation();
                
                ImGui::Separator();

                // Diff content
                switch (options_.mode)
                {
                case DiffViewMode::SideBySide:
                    RenderSideBySideView();
                    break;
                case DiffViewMode::Unified:
                    RenderUnifiedView();
                    break;
                case DiffViewMode::Inline:
                    RenderInlineView();
                    break;
                }
            }
        }
        ImGui::End();

        if (show_options_popup_)
        {
            RenderOptionsPopup();
        }
    }

    void DiffViewer::RenderToolbar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Side by Side", nullptr, options_.mode == DiffViewMode::SideBySide))
                    options_.mode = DiffViewMode::SideBySide;
                if (ImGui::MenuItem("Unified", nullptr, options_.mode == DiffViewMode::Unified))
                    options_.mode = DiffViewMode::Unified;
                if (ImGui::MenuItem("Inline", nullptr, options_.mode == DiffViewMode::Inline))
                    options_.mode = DiffViewMode::Inline;
                
                ImGui::Separator();
                
                ImGui::MenuItem("Line Numbers", nullptr, &options_.show_line_numbers);
                ImGui::MenuItem("Word Wrap", nullptr, &options_.word_wrap);
                ImGui::MenuItem("Show Whitespace", nullptr, &options_.show_whitespace);
                ImGui::MenuItem("Sync Scroll", nullptr, &sync_scroll_);
                
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Options"))
            {
                ImGui::MenuItem("Ignore Whitespace", nullptr, &diff_options_.ignore_whitespace);
                ImGui::MenuItem("Ignore Case", nullptr, &diff_options_.ignore_case);
                ImGui::MenuItem("Ignore Blank Lines", nullptr, &diff_options_.ignore_blank_lines);
                
                ImGui::Separator();
                
                if (ImGui::MenuItem("Refresh"))
                {
                    if (!left_path_.empty() && !right_path_.empty())
                    {
                        CompareFiles(left_path_, right_path_);
                    }
                }
                
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Export"))
            {
                if (ImGui::MenuItem("Export as Patch..."))
                {
                    // TODO: File dialog
                }
                if (ImGui::MenuItem("Export as HTML..."))
                {
                    // TODO: File dialog
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
    }

    void DiffViewer::RenderSideBySideView()
    {
        float available_width = ImGui::GetContentRegionAvail().x;
        float half_width = available_width / 2.0f - 4.0f;

        // Left panel header
        ImGui::BeginChild("LeftHeader", ImVec2(half_width, 24), true);
        ImGui::TextUnformatted(left_path_.c_str());
        ImGui::EndChild();

        ImGui::SameLine();

        // Right panel header
        ImGui::BeginChild("RightHeader", ImVec2(half_width, 24), true);
        ImGui::TextUnformatted(right_path_.c_str());
        ImGui::EndChild();

        // Content panels
        ImGui::BeginChild("LeftContent", ImVec2(half_width, 0), true, 
            ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& hunk : result_.hunks)
        {
            for (const auto& line : hunk.lines)
            {
                ImVec4 color = GetDiffTypeColor(line.type);
                
                if (options_.show_line_numbers && line.left_line_number > 0)
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%4zu ", line.left_line_number);
                    ImGui::SameLine();
                }

                if (line.type == DiffType::Added)
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "    ");
                }
                else
                {
                    ImGui::TextColored(color, "%s", line.left_text.c_str());
                }
            }
        }

        float left_scroll = ImGui::GetScrollY();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("RightContent", ImVec2(half_width, 0), true,
            ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& hunk : result_.hunks)
        {
            for (const auto& line : hunk.lines)
            {
                ImVec4 color = GetDiffTypeColor(line.type);
                
                if (options_.show_line_numbers && line.right_line_number > 0)
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%4zu ", line.right_line_number);
                    ImGui::SameLine();
                }

                if (line.type == DiffType::Removed)
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "    ");
                }
                else
                {
                    ImGui::TextColored(color, "%s", line.right_text.c_str());
                }
            }
        }

        // Sync scroll
        if (sync_scroll_)
        {
            float right_scroll = ImGui::GetScrollY();
            if (right_scroll != left_scroll)
            {
                ImGui::SetScrollY(left_scroll);
            }
        }

        ImGui::EndChild();
    }

    void DiffViewer::RenderUnifiedView()
    {
        ImGui::BeginChild("UnifiedContent", ImVec2(0, 0), true, 
            ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& hunk : result_.hunks)
        {
            // Hunk header
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), 
                "@@ -%zu,%zu +%zu,%zu @@",
                hunk.left_start, hunk.left_count,
                hunk.right_start, hunk.right_count);

            for (const auto& line : hunk.lines)
            {
                ImVec4 color = GetDiffTypeColor(line.type);
                char prefix = ' ';

                switch (line.type)
                {
                case DiffType::Added: prefix = '+'; break;
                case DiffType::Removed: prefix = '-'; break;
                case DiffType::Modified: prefix = '~'; break;
                default: prefix = ' '; break;
                }

                if (options_.show_line_numbers)
                {
                    std::string left_num = line.left_line_number > 0 
                        ? std::to_string(line.left_line_number) : "   ";
                    std::string right_num = line.right_line_number > 0 
                        ? std::to_string(line.right_line_number) : "   ";
                    
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), 
                        "%4s %4s ", left_num.c_str(), right_num.c_str());
                    ImGui::SameLine();
                }

                const std::string& text = (line.type == DiffType::Added) 
                    ? line.right_text : line.left_text;
                
                ImGui::TextColored(color, "%c %s", prefix, text.c_str());
            }
        }

        ImGui::EndChild();
    }

    void DiffViewer::RenderInlineView()
    {
        ImGui::BeginChild("InlineContent", ImVec2(0, 0), true,
            ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& hunk : result_.hunks)
        {
            for (const auto& line : hunk.lines)
            {
                ImVec4 color = GetDiffTypeColor(line.type);

                switch (line.type)
                {
                case DiffType::Equal:
                    ImGui::TextColored(color, "  %s", line.left_text.c_str());
                    break;
                case DiffType::Added:
                    ImGui::TextColored(color, "+ %s", line.right_text.c_str());
                    break;
                case DiffType::Removed:
                    ImGui::TextColored(color, "- %s", line.left_text.c_str());
                    break;
                case DiffType::Modified:
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "- %s", line.left_text.c_str());
                    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "+ %s", line.right_text.c_str());
                    break;
                }
            }
        }

        ImGui::EndChild();
    }

    void DiffViewer::RenderHunkNavigation()
    {
        ImGui::SameLine(ImGui::GetWindowWidth() - 200);
        
        if (ImGui::Button("<< Prev"))
            PreviousDifference();
        
        ImGui::SameLine();
        ImGui::Text("%zu / %zu", 
            result_.hunks.empty() ? 0 : current_hunk_ + 1, 
            result_.hunks.size());
        
        ImGui::SameLine();
        if (ImGui::Button("Next >>"))
            NextDifference();
    }

    void DiffViewer::RenderOptionsPopup()
    {
        if (ImGui::BeginPopupModal("Diff Options", &show_options_popup_))
        {
            ImGui::Text("Context lines:");
            ImGui::SliderInt("##Context", &options_.context_lines, 0, 10);

            ImGui::Separator();

            ImGui::Checkbox("Ignore whitespace", &diff_options_.ignore_whitespace);
            ImGui::Checkbox("Ignore case", &diff_options_.ignore_case);
            ImGui::Checkbox("Ignore blank lines", &diff_options_.ignore_blank_lines);

            ImGui::Separator();

            if (ImGui::Button("OK"))
            {
                show_options_popup_ = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    ImVec4 DiffViewer::GetDiffTypeColor(diff::DiffType type) const
    {
        switch (type)
        {
        case diff::DiffType::Added:
            return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);  // Green
        case diff::DiffType::Removed:
            return ImVec4(0.9f, 0.4f, 0.4f, 1.0f);  // Red
        case diff::DiffType::Modified:
            return ImVec4(0.9f, 0.9f, 0.4f, 1.0f);  // Yellow
        case diff::DiffType::Equal:
        default:
            return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);  // Gray
        }
    }

    std::string DiffViewer::FormatLineNumber(size_t num, int width) const
    {
        std::ostringstream ss;
        ss << std::setw(width) << num;
        return ss.str();
    }

    bool DiffViewer::ExportToFile(const std::string& path, bool as_html) const
    {
        std::ofstream file(path);
        if (!file)
            return false;

        if (as_html)
        {
            file << engine_.ExportToHtml(result_);
        }
        else
        {
            file << engine_.GenerateUnifiedDiff(result_);
        }

        return true;
    }

} // namespace opacity::ui
