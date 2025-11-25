#include "opacity/diff/DiffEngine.h"
#include "opacity/core/Logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace opacity::diff
{
    DiffEngine::DiffEngine() = default;
    DiffEngine::~DiffEngine() = default;

    DiffResult DiffEngine::CompareFiles(
        const core::Path& left_path,
        const core::Path& right_path,
        const DiffOptions& options)
    {
        DiffResult result;
        result.left_file = left_path.String();
        result.right_file = right_path.String();

        // Read left file
        std::ifstream left_stream(left_path.String());
        if (!left_stream)
        {
            result.error_message = "Failed to open left file: " + left_path.String();
            SPDLOG_ERROR(result.error_message);
            return result;
        }

        std::stringstream left_buffer;
        left_buffer << left_stream.rdbuf();
        std::string left_content = left_buffer.str();

        // Read right file
        std::ifstream right_stream(right_path.String());
        if (!right_stream)
        {
            result.error_message = "Failed to open right file: " + right_path.String();
            SPDLOG_ERROR(result.error_message);
            return result;
        }

        std::stringstream right_buffer;
        right_buffer << right_stream.rdbuf();
        std::string right_content = right_buffer.str();

        return CompareText(left_content, right_content, options);
    }

    DiffResult DiffEngine::CompareText(
        const std::string& left_text,
        const std::string& right_text,
        const DiffOptions& options)
    {
        DiffResult result;

        auto left_lines = SplitLines(left_text);
        auto right_lines = SplitLines(right_text);

        auto diff_lines = ComputeDiff(left_lines, right_lines, options);
        result.hunks = CreateHunks(diff_lines, options.context_lines);

        // Calculate statistics
        for (const auto& line : diff_lines)
        {
            switch (line.type)
            {
            case DiffType::Added:
                ++result.lines_added;
                break;
            case DiffType::Removed:
                ++result.lines_removed;
                break;
            case DiffType::Modified:
                ++result.lines_modified;
                break;
            default:
                break;
            }
        }

        result.total_changes = result.lines_added + result.lines_removed + result.lines_modified;
        result.success = true;

        SPDLOG_DEBUG("Diff complete: {} added, {} removed, {} modified",
            result.lines_added, result.lines_removed, result.lines_modified);

        return result;
    }

    BinaryDiffResult DiffEngine::CompareBinaryFiles(
        const core::Path& left_path,
        const core::Path& right_path)
    {
        BinaryDiffResult result;
        result.left_file = left_path.String();
        result.right_file = right_path.String();

        std::ifstream left_stream(left_path.String(), std::ios::binary | std::ios::ate);
        std::ifstream right_stream(right_path.String(), std::ios::binary | std::ios::ate);

        if (!left_stream)
        {
            result.error_message = "Failed to open left file: " + left_path.String();
            return result;
        }

        if (!right_stream)
        {
            result.error_message = "Failed to open right file: " + right_path.String();
            return result;
        }

        result.left_size = static_cast<uint64_t>(left_stream.tellg());
        result.right_size = static_cast<uint64_t>(right_stream.tellg());

        left_stream.seekg(0);
        right_stream.seekg(0);

        // Compare byte by byte
        const size_t buffer_size = 65536;
        std::vector<char> left_buffer(buffer_size);
        std::vector<char> right_buffer(buffer_size);
        
        size_t offset = 0;
        bool found_difference = false;

        while (left_stream && right_stream)
        {
            left_stream.read(left_buffer.data(), buffer_size);
            right_stream.read(right_buffer.data(), buffer_size);

            auto left_read = left_stream.gcount();
            auto right_read = right_stream.gcount();

            auto compare_size = std::min(left_read, right_read);

            for (std::streamsize i = 0; i < compare_size; ++i)
            {
                if (left_buffer[i] != right_buffer[i])
                {
                    result.first_difference_offset = offset + static_cast<size_t>(i);
                    found_difference = true;
                    break;
                }
            }

            if (found_difference)
                break;

            if (left_read != right_read)
            {
                result.first_difference_offset = offset + static_cast<size_t>(std::min(left_read, right_read));
                found_difference = true;
                break;
            }

            offset += static_cast<size_t>(compare_size);
        }

        result.are_identical = !found_difference && (result.left_size == result.right_size);
        result.success = true;

        return result;
    }

    std::string DiffEngine::GenerateUnifiedDiff(const DiffResult& result) const
    {
        std::stringstream ss;

        ss << "--- " << result.left_file << "\n";
        ss << "+++ " << result.right_file << "\n";

        for (const auto& hunk : result.hunks)
        {
            ss << "@@ -" << hunk.left_start << "," << hunk.left_count
               << " +" << hunk.right_start << "," << hunk.right_count << " @@\n";

            for (const auto& line : hunk.lines)
            {
                switch (line.type)
                {
                case DiffType::Equal:
                    ss << " " << line.left_text << "\n";
                    break;
                case DiffType::Removed:
                    ss << "-" << line.left_text << "\n";
                    break;
                case DiffType::Added:
                    ss << "+" << line.right_text << "\n";
                    break;
                case DiffType::Modified:
                    ss << "-" << line.left_text << "\n";
                    ss << "+" << line.right_text << "\n";
                    break;
                }
            }
        }

        return ss.str();
    }

    std::string DiffEngine::GeneratePatch(const DiffResult& result) const
    {
        return GenerateUnifiedDiff(result);
    }

    std::string DiffEngine::ExportToHtml(const DiffResult& result) const
    {
        std::stringstream ss;

        ss << R"(<!DOCTYPE html>
<html>
<head>
<style>
body { font-family: monospace; }
.diff-table { border-collapse: collapse; width: 100%; }
.line-num { background: #f0f0f0; padding: 0 8px; text-align: right; color: #666; }
.line-content { padding: 0 8px; white-space: pre; }
.added { background: #e6ffe6; }
.removed { background: #ffe6e6; }
.modified { background: #fff3cd; }
</style>
</head>
<body>
<h2>Diff: )" << result.left_file << " vs " << result.right_file << R"(</h2>
<p>Added: )" << result.lines_added << R"(, Removed: )" << result.lines_removed 
           << R"(, Modified: )" << result.lines_modified << R"(</p>
<table class="diff-table">
)";

        for (const auto& hunk : result.hunks)
        {
            ss << "<tr><th colspan='4'>@@ -" << hunk.left_start << "," << hunk.left_count
               << " +" << hunk.right_start << "," << hunk.right_count << " @@</th></tr>\n";

            for (const auto& line : hunk.lines)
            {
                std::string css_class;
                switch (line.type)
                {
                case DiffType::Added: css_class = "added"; break;
                case DiffType::Removed: css_class = "removed"; break;
                case DiffType::Modified: css_class = "modified"; break;
                default: break;
                }

                ss << "<tr class='" << css_class << "'>";
                ss << "<td class='line-num'>" << (line.left_line_number > 0 ? std::to_string(line.left_line_number) : "") << "</td>";
                ss << "<td class='line-content'>" << line.left_text << "</td>";
                ss << "<td class='line-num'>" << (line.right_line_number > 0 ? std::to_string(line.right_line_number) : "") << "</td>";
                ss << "<td class='line-content'>" << line.right_text << "</td>";
                ss << "</tr>\n";
            }
        }

        ss << R"(</table>
</body>
</html>)";

        return ss.str();
    }

    bool DiffEngine::IsBinaryFile(const core::Path& path)
    {
        std::ifstream file(path.String(), std::ios::binary);
        if (!file)
            return false;

        // Check first 8KB for null bytes
        const size_t check_size = 8192;
        std::vector<char> buffer(check_size);
        file.read(buffer.data(), check_size);
        auto bytes_read = file.gcount();

        for (std::streamsize i = 0; i < bytes_read; ++i)
        {
            if (buffer[i] == '\0')
                return true;
        }

        return false;
    }

    std::string DiffEngine::CalculateFileHash(const core::Path& path) const
    {
        // Simple hash for demonstration - in production use SHA-256 or similar
        std::ifstream file(path.String(), std::ios::binary);
        if (!file)
            return "";

        uint64_t hash = 0;
        char buffer[4096];

        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
        {
            for (std::streamsize i = 0; i < file.gcount(); ++i)
            {
                hash = hash * 31 + static_cast<unsigned char>(buffer[i]);
            }
        }

        std::stringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }

    std::vector<std::string> DiffEngine::SplitLines(const std::string& text) const
    {
        std::vector<std::string> lines;
        std::string line;
        std::istringstream stream(text);

        while (std::getline(stream, line))
        {
            // Remove trailing \r if present (for Windows line endings)
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }

        return lines;
    }

    std::string DiffEngine::NormalizeLine(const std::string& line, const DiffOptions& options) const
    {
        std::string result = line;

        if (options.ignore_case)
        {
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        }

        if (options.ignore_leading_whitespace || options.ignore_whitespace)
        {
            size_t start = result.find_first_not_of(" \t");
            if (start != std::string::npos)
                result = result.substr(start);
            else
                result.clear();
        }

        if (options.ignore_trailing_whitespace || options.ignore_whitespace)
        {
            size_t end = result.find_last_not_of(" \t");
            if (end != std::string::npos)
                result = result.substr(0, end + 1);
            else
                result.clear();
        }

        if (options.ignore_whitespace)
        {
            // Collapse all whitespace to single space
            std::string collapsed;
            bool in_space = false;
            for (char c : result)
            {
                if (c == ' ' || c == '\t')
                {
                    if (!in_space)
                    {
                        collapsed += ' ';
                        in_space = true;
                    }
                }
                else
                {
                    collapsed += c;
                    in_space = false;
                }
            }
            result = collapsed;
        }

        return result;
    }

    std::vector<DiffLine> DiffEngine::ComputeDiff(
        const std::vector<std::string>& left_lines,
        const std::vector<std::string>& right_lines,
        const DiffOptions& options) const
    {
        std::vector<DiffLine> result;

        // Simple LCS-based diff algorithm
        // In production, use Myers algorithm for better performance

        size_t left_size = left_lines.size();
        size_t right_size = right_lines.size();

        // Create normalized versions for comparison
        std::vector<std::string> left_norm, right_norm;
        for (const auto& line : left_lines)
            left_norm.push_back(NormalizeLine(line, options));
        for (const auto& line : right_lines)
            right_norm.push_back(NormalizeLine(line, options));

        // Build LCS table
        std::vector<std::vector<size_t>> lcs(left_size + 1, std::vector<size_t>(right_size + 1, 0));

        for (size_t i = 1; i <= left_size; ++i)
        {
            for (size_t j = 1; j <= right_size; ++j)
            {
                if (left_norm[i - 1] == right_norm[j - 1])
                    lcs[i][j] = lcs[i - 1][j - 1] + 1;
                else
                    lcs[i][j] = std::max(lcs[i - 1][j], lcs[i][j - 1]);
            }
        }

        // Backtrack to find differences
        std::vector<DiffLine> temp_result;
        size_t i = left_size;
        size_t j = right_size;

        while (i > 0 || j > 0)
        {
            DiffLine line;

            if (i > 0 && j > 0 && left_norm[i - 1] == right_norm[j - 1])
            {
                line.type = DiffType::Equal;
                line.left_text = left_lines[i - 1];
                line.right_text = right_lines[j - 1];
                line.left_line_number = i;
                line.right_line_number = j;
                --i;
                --j;
            }
            else if (j > 0 && (i == 0 || lcs[i][j - 1] >= lcs[i - 1][j]))
            {
                line.type = DiffType::Added;
                line.right_text = right_lines[j - 1];
                line.right_line_number = j;
                --j;
            }
            else if (i > 0)
            {
                line.type = DiffType::Removed;
                line.left_text = left_lines[i - 1];
                line.left_line_number = i;
                --i;
            }

            temp_result.push_back(line);
        }

        // Reverse to get correct order
        std::reverse(temp_result.begin(), temp_result.end());

        // Filter blank lines if requested
        if (options.ignore_blank_lines)
        {
            for (const auto& line : temp_result)
            {
                bool left_blank = line.left_text.find_first_not_of(" \t\r\n") == std::string::npos;
                bool right_blank = line.right_text.find_first_not_of(" \t\r\n") == std::string::npos;

                if (line.type == DiffType::Equal || (!left_blank && !right_blank))
                {
                    result.push_back(line);
                }
            }
        }
        else
        {
            result = std::move(temp_result);
        }

        return result;
    }

    std::vector<DiffHunk> DiffEngine::CreateHunks(
        const std::vector<DiffLine>& lines,
        int context_lines) const
    {
        std::vector<DiffHunk> hunks;

        if (lines.empty())
            return hunks;

        // Find ranges of changes with context
        std::vector<std::pair<size_t, size_t>> change_ranges;
        size_t i = 0;

        while (i < lines.size())
        {
            // Skip equal lines
            while (i < lines.size() && lines[i].type == DiffType::Equal)
                ++i;

            if (i >= lines.size())
                break;

            // Found a change, determine range with context
            size_t start = (i >= static_cast<size_t>(context_lines)) ? i - context_lines : 0;
            
            // Find end of this change block
            size_t end = i;
            while (end < lines.size())
            {
                if (lines[end].type != DiffType::Equal)
                {
                    end++;
                }
                else
                {
                    // Count consecutive equal lines
                    size_t equal_count = 0;
                    size_t j = end;
                    while (j < lines.size() && lines[j].type == DiffType::Equal)
                    {
                        ++equal_count;
                        ++j;
                    }

                    if (equal_count > static_cast<size_t>(context_lines * 2) || j >= lines.size())
                    {
                        // Too many equal lines or end of file, end this hunk
                        break;
                    }
                    else
                    {
                        // Include these equal lines and continue
                        end = j;
                    }
                }
            }

            // Add context after
            end = std::min(end + static_cast<size_t>(context_lines), lines.size());

            change_ranges.emplace_back(start, end);
            i = end;
        }

        // Create hunks from ranges
        for (const auto& range : change_ranges)
        {
            DiffHunk hunk;
            
            for (size_t j = range.first; j < range.second; ++j)
            {
                hunk.lines.push_back(lines[j]);
                
                if (lines[j].left_line_number > 0)
                {
                    if (hunk.left_start == 0)
                        hunk.left_start = lines[j].left_line_number;
                    ++hunk.left_count;
                }
                
                if (lines[j].right_line_number > 0)
                {
                    if (hunk.right_start == 0)
                        hunk.right_start = lines[j].right_line_number;
                    ++hunk.right_count;
                }
            }

            if (!hunk.lines.empty())
                hunks.push_back(std::move(hunk));
        }

        return hunks;
    }

} // namespace opacity::diff