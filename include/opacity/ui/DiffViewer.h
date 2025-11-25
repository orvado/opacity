#pragma once

#include "opacity/diff/DiffEngine.h"
#include <imgui.h>
#include <string>
#include <functional>

namespace opacity::ui
{
    /**
     * @brief Display mode for diff viewer
     */
    enum class DiffViewMode
    {
        SideBySide,     // Two columns showing left and right
        Unified,        // Single column with +/- markers
        Inline          // Interleaved view
    };

    /**
     * @brief Options for diff viewer display
     */
    struct DiffViewerOptions
    {
        DiffViewMode mode = DiffViewMode::SideBySide;
        bool show_line_numbers = true;
        bool word_wrap = false;
        bool syntax_highlighting = true;
        bool show_whitespace = false;
        int context_lines = 3;
        float font_size = 14.0f;
    };

    /**
     * @brief Diff viewer UI component for Phase 2
     * 
     * Features:
     * - Side-by-side and unified diff views
     * - Navigation between differences
     * - Syntax highlighting
     * - Line numbers
     * - Scrolling synchronization
     */
    class DiffViewer
    {
    public:
        using CompareCallback = std::function<void(const std::string&, const std::string&)>;

        DiffViewer();
        ~DiffViewer();

        /**
         * @brief Show the diff viewer window
         */
        void Show();

        /**
         * @brief Hide the diff viewer window
         */
        void Hide();

        /**
         * @brief Check if viewer is visible
         */
        bool IsVisible() const { return visible_; }

        /**
         * @brief Load a diff result for display
         */
        void LoadDiff(const diff::DiffResult& result);

        /**
         * @brief Compare two files
         */
        void CompareFiles(const std::string& left_path, const std::string& right_path);

        /**
         * @brief Compare two text strings
         */
        void CompareText(const std::string& left_text, const std::string& right_text,
                        const std::string& left_name = "Left",
                        const std::string& right_name = "Right");

        /**
         * @brief Set view options
         */
        void SetOptions(const DiffViewerOptions& options) { options_ = options; }
        const DiffViewerOptions& GetOptions() const { return options_; }

        /**
         * @brief Set diff options
         */
        void SetDiffOptions(const diff::DiffOptions& options) { diff_options_ = options; }

        /**
         * @brief Navigate to next difference
         */
        void NextDifference();

        /**
         * @brief Navigate to previous difference
         */
        void PreviousDifference();

        /**
         * @brief Go to specific hunk
         */
        void GoToHunk(size_t index);

        /**
         * @brief Get current hunk index
         */
        size_t GetCurrentHunk() const { return current_hunk_; }

        /**
         * @brief Get total hunk count
         */
        size_t GetHunkCount() const;

        /**
         * @brief Render the diff viewer
         */
        void Render();

        /**
         * @brief Export diff to file
         */
        bool ExportToFile(const std::string& path, bool as_html = false) const;

    private:
        void RenderToolbar();
        void RenderSideBySideView();
        void RenderUnifiedView();
        void RenderInlineView();
        void RenderHunkNavigation();
        void RenderOptionsPopup();

        void SyncScroll(float scroll_y);
        ImVec4 GetDiffTypeColor(diff::DiffType type) const;
        std::string FormatLineNumber(size_t num, int width) const;

        bool visible_ = false;
        diff::DiffResult result_;
        diff::DiffEngine engine_;
        
        DiffViewerOptions options_;
        diff::DiffOptions diff_options_;

        // Navigation state
        size_t current_hunk_ = 0;
        float scroll_y_ = 0.0f;
        bool sync_scroll_ = true;

        // File paths
        std::string left_path_;
        std::string right_path_;

        // UI state
        bool show_options_popup_ = false;
    };

} // namespace opacity::ui
