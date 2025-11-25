#pragma once

#include "opacity/core/Path.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace opacity::diff
{
    /**
     * @brief Type of difference detected
     */
    enum class DiffType
    {
        Equal,      // Lines are identical
        Added,      // Line exists only in right/new file
        Removed,    // Line exists only in left/old file
        Modified    // Line was changed
    };

    /**
     * @brief Single line in a diff result
     */
    struct DiffLine
    {
        DiffType type = DiffType::Equal;
        std::string left_text;          // Original/left file content
        std::string right_text;         // New/right file content
        size_t left_line_number = 0;    // Line number in left file (0 if not present)
        size_t right_line_number = 0;   // Line number in right file (0 if not present)
    };

    /**
     * @brief A contiguous block of changes
     */
    struct DiffHunk
    {
        size_t left_start = 0;          // Starting line in left file
        size_t left_count = 0;          // Number of lines from left file
        size_t right_start = 0;         // Starting line in right file
        size_t right_count = 0;         // Number of lines from right file
        std::vector<DiffLine> lines;    // Lines in this hunk
    };

    /**
     * @brief Options for diff computation
     */
    struct DiffOptions
    {
        bool ignore_whitespace = false;         // Ignore all whitespace
        bool ignore_leading_whitespace = false; // Ignore leading whitespace
        bool ignore_trailing_whitespace = false;// Ignore trailing whitespace
        bool ignore_case = false;               // Case-insensitive comparison
        bool ignore_blank_lines = false;        // Ignore blank lines
        int context_lines = 3;                  // Lines of context around changes
    };

    /**
     * @brief Result of a diff operation
     */
    struct DiffResult
    {
        std::string left_file;
        std::string right_file;
        std::vector<DiffHunk> hunks;
        
        // Statistics
        size_t lines_added = 0;
        size_t lines_removed = 0;
        size_t lines_modified = 0;
        size_t total_changes = 0;
        
        bool success = false;
        std::string error_message;

        /**
         * @brief Check if files are identical
         */
        bool AreIdentical() const { return success && total_changes == 0; }
    };

    /**
     * @brief Binary comparison result
     */
    struct BinaryDiffResult
    {
        std::string left_file;
        std::string right_file;
        bool are_identical = false;
        
        uint64_t left_size = 0;
        uint64_t right_size = 0;
        size_t first_difference_offset = 0;
        
        bool success = false;
        std::string error_message;
    };

    /**
     * @brief File comparison engine for Phase 2
     * 
     * Features:
     * - Line-by-line text diff (Myers algorithm)
     * - Binary file comparison
     * - Diff options (ignore whitespace, case, etc.)
     * - Unified and side-by-side output
     */
    class DiffEngine
    {
    public:
        DiffEngine();
        ~DiffEngine();

        /**
         * @brief Compare two text files
         * @param left_path Path to first/original file
         * @param right_path Path to second/new file
         * @param options Comparison options
         * @return Diff result with hunks
         */
        DiffResult CompareFiles(
            const core::Path& left_path,
            const core::Path& right_path,
            const DiffOptions& options = DiffOptions());

        /**
         * @brief Compare two strings/text blocks
         */
        DiffResult CompareText(
            const std::string& left_text,
            const std::string& right_text,
            const DiffOptions& options = DiffOptions());

        /**
         * @brief Binary comparison of two files
         */
        BinaryDiffResult CompareBinaryFiles(
            const core::Path& left_path,
            const core::Path& right_path);

        /**
         * @brief Generate unified diff format output
         */
        std::string GenerateUnifiedDiff(const DiffResult& result) const;

        /**
         * @brief Generate patch file content
         */
        std::string GeneratePatch(const DiffResult& result) const;

        /**
         * @brief Export diff to HTML for viewing
         */
        std::string ExportToHtml(const DiffResult& result) const;

        /**
         * @brief Check if a file appears to be binary
         */
        static bool IsBinaryFile(const core::Path& path);

        /**
         * @brief Calculate hash/checksum for integrity verification
         */
        std::string CalculateFileHash(const core::Path& path) const;

    private:
        std::vector<std::string> SplitLines(const std::string& text) const;
        std::string NormalizeLine(const std::string& line, const DiffOptions& options) const;
        
        // Myers diff algorithm implementation
        std::vector<DiffLine> ComputeDiff(
            const std::vector<std::string>& left_lines,
            const std::vector<std::string>& right_lines,
            const DiffOptions& options) const;

        std::vector<DiffHunk> CreateHunks(
            const std::vector<DiffLine>& lines,
            int context_lines) const;
    };

} // namespace opacity::diff
