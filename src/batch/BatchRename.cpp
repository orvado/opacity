#include "opacity/batch/BatchRename.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <random>
#include <sstream>

namespace opacity::batch
{
    BatchRename::BatchRename() = default;
    BatchRename::~BatchRename() = default;

    void BatchRename::AddFiles(const std::vector<core::Path>& paths)
    {
        for (const auto& path : paths)
        {
            if (std::filesystem::exists(path.Get()) && 
                std::filesystem::is_regular_file(path.Get()))
            {
                files_.push_back(path);
            }
        }
    }

    void BatchRename::ClearFiles()
    {
        files_.clear();
    }

    void BatchRename::AddRule(const RenameRule& rule)
    {
        rules_.push_back(rule);
    }

    void BatchRename::RemoveRule(size_t index)
    {
        if (index < rules_.size())
        {
            rules_.erase(rules_.begin() + index);
        }
    }

    void BatchRename::ClearRules()
    {
        rules_.clear();
    }

    void BatchRename::MoveRuleUp(size_t index)
    {
        if (index > 0 && index < rules_.size())
        {
            std::swap(rules_[index], rules_[index - 1]);
        }
    }

    void BatchRename::MoveRuleDown(size_t index)
    {
        if (index < rules_.size() - 1)
        {
            std::swap(rules_[index], rules_[index + 1]);
        }
    }

    std::vector<RenamePreview> BatchRename::GeneratePreview()
    {
        std::vector<RenamePreview> previews;
        previews.reserve(files_.size());

        for (size_t i = 0; i < files_.size(); ++i)
        {
            RenamePreview preview;
            preview.original_path = files_[i];
            preview.original_name = files_[i].Filename();

            try
            {
                preview.new_name = ApplyAllRules(preview.original_name, i);
                preview.will_change = (preview.original_name != preview.new_name);
            }
            catch (const std::exception& e)
            {
                preview.has_error = true;
                preview.error_message = e.what();
                preview.new_name = preview.original_name;
            }

            previews.push_back(preview);
        }

        // Check for conflicts
        for (size_t i = 0; i < previews.size(); ++i)
        {
            if (previews[i].has_error) continue;

            core::Path new_path(files_[i].Parent().String() + "/" + previews[i].new_name);
            previews[i].has_conflict = HasConflict(new_path, previews, i);
        }

        return previews;
    }

    std::string BatchRename::ApplyRule(const std::string& filename, 
                                        const RenameRule& rule, 
                                        size_t file_index)
    {
        auto [name, ext] = SplitExtension(filename);
        std::string result_name = name;
        std::string result_ext = ext;

        switch (rule.operation)
        {
        case RenameOperation::Replace:
            {
                if (rule.use_regex)
                {
                    try
                    {
                        std::regex rx(rule.find_text, 
                            rule.case_sensitive ? std::regex::ECMAScript : 
                                                  std::regex::ECMAScript | std::regex::icase);
                        result_name = std::regex_replace(result_name, rx, rule.replace_text);
                        if (rule.apply_to_extension)
                        {
                            result_ext = std::regex_replace(result_ext, rx, rule.replace_text);
                        }
                    }
                    catch (const std::regex_error& e)
                    {
                        SPDLOG_WARN("Regex error: {}", e.what());
                    }
                }
                else
                {
                    // Simple find and replace
                    std::string search = rule.find_text;
                    std::string target = result_name;
                    
                    if (!rule.case_sensitive)
                    {
                        std::transform(search.begin(), search.end(), search.begin(), ::tolower);
                        std::transform(target.begin(), target.end(), target.begin(), ::tolower);
                    }

                    size_t pos = 0;
                    while ((pos = target.find(search, pos)) != std::string::npos)
                    {
                        result_name.replace(pos, rule.find_text.length(), rule.replace_text);
                        if (!rule.case_sensitive)
                        {
                            target = result_name;
                            std::transform(target.begin(), target.end(), target.begin(), ::tolower);
                        }
                        pos += rule.replace_text.length();
                    }
                }
            }
            break;

        case RenameOperation::RegexReplace:
            {
                try
                {
                    std::regex rx(rule.find_text,
                        rule.case_sensitive ? std::regex::ECMAScript :
                                              std::regex::ECMAScript | std::regex::icase);
                    result_name = std::regex_replace(result_name, rx, rule.replace_text);
                }
                catch (const std::regex_error& e)
                {
                    SPDLOG_WARN("Regex error: {}", e.what());
                }
            }
            break;

        case RenameOperation::AddPrefix:
            result_name = rule.replace_text + result_name;
            break;

        case RenameOperation::AddSuffix:
            result_name = result_name + rule.replace_text;
            break;

        case RenameOperation::RemoveCharacters:
            {
                std::string chars_to_remove = rule.find_text;
                result_name.erase(
                    std::remove_if(result_name.begin(), result_name.end(),
                        [&chars_to_remove](char c) {
                            return chars_to_remove.find(c) != std::string::npos;
                        }),
                    result_name.end()
                );
            }
            break;

        case RenameOperation::ChangeCase:
            result_name = ApplyCaseChange(result_name, rule.case_change);
            if (rule.apply_to_extension)
            {
                result_ext = ApplyCaseChange(result_ext, rule.case_change);
            }
            break;

        case RenameOperation::InsertText:
            {
                std::string text = rule.replace_text;
                switch (rule.position)
                {
                case InsertPosition::Start:
                    result_name = text + result_name;
                    break;
                case InsertPosition::End:
                    result_name = result_name + text;
                    break;
                case InsertPosition::AtIndex:
                    if (static_cast<size_t>(rule.insert_index) <= result_name.length())
                    {
                        result_name.insert(rule.insert_index, text);
                    }
                    break;
                case InsertPosition::BeforeText:
                    {
                        size_t pos = result_name.find(rule.find_text);
                        if (pos != std::string::npos)
                        {
                            result_name.insert(pos, text);
                        }
                    }
                    break;
                case InsertPosition::AfterText:
                    {
                        size_t pos = result_name.find(rule.find_text);
                        if (pos != std::string::npos)
                        {
                            result_name.insert(pos + rule.find_text.length(), text);
                        }
                    }
                    break;
                case InsertPosition::ReplaceFilename:
                    result_name = text;
                    break;
                }
            }
            break;

        case RenameOperation::TrimText:
            {
                if (rule.trim_start > 0 && static_cast<size_t>(rule.trim_start) < result_name.length())
                {
                    result_name = result_name.substr(rule.trim_start);
                }
                if (rule.trim_end > 0 && static_cast<size_t>(rule.trim_end) < result_name.length())
                {
                    result_name = result_name.substr(0, result_name.length() - rule.trim_end);
                }
            }
            break;

        case RenameOperation::Numbering:
            {
                int number = rule.start_number + static_cast<int>(file_index) * rule.increment;
                std::string num_str = FormatNumber(number, rule.number_style, rule.padding);
                std::string formatted = rule.number_prefix + num_str + rule.number_suffix;

                switch (rule.position)
                {
                case InsertPosition::Start:
                    result_name = formatted + result_name;
                    break;
                case InsertPosition::End:
                    result_name = result_name + formatted;
                    break;
                case InsertPosition::ReplaceFilename:
                    result_name = formatted;
                    break;
                default:
                    result_name = formatted + result_name;
                    break;
                }
            }
            break;

        case RenameOperation::Extension:
            result_ext = rule.replace_text;
            break;

        default:
            break;
        }

        // Combine name and extension
        if (result_ext.empty())
        {
            return result_name;
        }
        return result_name + "." + result_ext;
    }

    std::string BatchRename::ApplyAllRules(const std::string& filename, size_t file_index)
    {
        std::string result = filename;
        for (const auto& rule : rules_)
        {
            result = ApplyRule(result, rule, file_index);
        }
        return result;
    }

    RenameResult BatchRename::Execute(RenameProgressCallback progress_callback)
    {
        RenameResult result;
        result.total_files = files_.size();

        auto previews = GeneratePreview();
        std::vector<std::pair<core::Path, core::Path>> undo_entries;

        for (size_t i = 0; i < files_.size(); ++i)
        {
            const auto& preview = previews[i];

            if (progress_callback)
            {
                RenameProgress progress;
                progress.files_processed = i;
                progress.total_files = files_.size();
                progress.current_file = preview.original_name;
                progress.percentage = files_.size() > 0 ? 
                    (static_cast<double>(i) / files_.size()) * 100.0 : 0.0;
                progress_callback(progress);
            }

            if (preview.has_error)
            {
                ++result.error_count;
                result.errors.push_back(preview.original_name + ": " + preview.error_message);
                continue;
            }

            if (!preview.will_change)
            {
                ++result.skipped_count;
                continue;
            }

            if (preview.has_conflict)
            {
                ++result.error_count;
                result.errors.push_back(preview.original_name + ": Name conflict with " + preview.new_name);
                continue;
            }

            core::Path new_path(files_[i].Parent().String() + "/" + preview.new_name);

            try
            {
                std::filesystem::rename(files_[i].Get(), new_path.Get());
                undo_entries.push_back({new_path, files_[i]});
                ++result.renamed_count;
                
                // Update the file list
                files_[i] = new_path;
            }
            catch (const std::exception& e)
            {
                ++result.error_count;
                result.errors.push_back(preview.original_name + ": " + e.what());
            }
        }

        // Save undo information
        if (!undo_entries.empty())
        {
            undo_stack_.push_back(undo_entries);
            while (undo_stack_.size() > MAX_UNDO_LEVELS)
            {
                undo_stack_.erase(undo_stack_.begin());
            }
        }

        result.success = result.error_count == 0;
        return result;
    }

    bool BatchRename::Undo()
    {
        if (undo_stack_.empty())
        {
            return false;
        }

        auto& last_operation = undo_stack_.back();
        bool all_success = true;

        for (const auto& [new_path, original_path] : last_operation)
        {
            try
            {
                std::filesystem::rename(new_path.Get(), original_path.Get());
                
                // Update file list
                auto it = std::find_if(files_.begin(), files_.end(),
                    [&new_path](const core::Path& p) { return p.String() == new_path.String(); });
                if (it != files_.end())
                {
                    *it = original_path;
                }
            }
            catch (const std::exception& e)
            {
                SPDLOG_ERROR("Failed to undo rename: {}", e.what());
                all_success = false;
            }
        }

        undo_stack_.pop_back();
        return all_success;
    }

    void BatchRename::SortByName(bool ascending)
    {
        std::sort(files_.begin(), files_.end(),
            [ascending](const core::Path& a, const core::Path& b)
            {
                return ascending ? a.Filename() < b.Filename() : a.Filename() > b.Filename();
            });
    }

    void BatchRename::SortByDate(bool ascending)
    {
        std::sort(files_.begin(), files_.end(),
            [ascending](const core::Path& a, const core::Path& b)
            {
                auto time_a = std::filesystem::last_write_time(a.Get());
                auto time_b = std::filesystem::last_write_time(b.Get());
                return ascending ? time_a < time_b : time_a > time_b;
            });
    }

    void BatchRename::SortBySize(bool ascending)
    {
        std::sort(files_.begin(), files_.end(),
            [ascending](const core::Path& a, const core::Path& b)
            {
                auto size_a = std::filesystem::file_size(a.Get());
                auto size_b = std::filesystem::file_size(b.Get());
                return ascending ? size_a < size_b : size_a > size_b;
            });
    }

    void BatchRename::Randomize()
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(files_.begin(), files_.end(), g);
    }

    void BatchRename::Reverse()
    {
        std::reverse(files_.begin(), files_.end());
    }

    RenameRule BatchRename::CreateReplaceRule(const std::string& find,
                                               const std::string& replace,
                                               bool case_sensitive)
    {
        RenameRule rule;
        rule.operation = RenameOperation::Replace;
        rule.find_text = find;
        rule.replace_text = replace;
        rule.case_sensitive = case_sensitive;
        return rule;
    }

    RenameRule BatchRename::CreateRegexRule(const std::string& pattern,
                                            const std::string& replace)
    {
        RenameRule rule;
        rule.operation = RenameOperation::RegexReplace;
        rule.find_text = pattern;
        rule.replace_text = replace;
        rule.use_regex = true;
        return rule;
    }

    RenameRule BatchRename::CreateNumberingRule(int start, int padding, InsertPosition pos)
    {
        RenameRule rule;
        rule.operation = RenameOperation::Numbering;
        rule.start_number = start;
        rule.padding = padding;
        rule.position = pos;
        rule.number_style = NumberingStyle::DecimalPadded;
        return rule;
    }

    RenameRule BatchRename::CreateCaseRule(CaseChange case_type, bool include_extension)
    {
        RenameRule rule;
        rule.operation = RenameOperation::ChangeCase;
        rule.case_change = case_type;
        rule.apply_to_extension = include_extension;
        return rule;
    }

    RenameRule BatchRename::CreateExtensionRule(const std::string& new_extension)
    {
        RenameRule rule;
        rule.operation = RenameOperation::Extension;
        rule.replace_text = new_extension;
        return rule;
    }

    std::string BatchRename::ApplyCaseChange(const std::string& text, CaseChange change) const
    {
        std::string result = text;

        switch (change)
        {
        case CaseChange::Lowercase:
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
            break;

        case CaseChange::Uppercase:
            std::transform(result.begin(), result.end(), result.begin(), ::toupper);
            break;

        case CaseChange::TitleCase:
            {
                bool capitalize_next = true;
                for (char& c : result)
                {
                    if (std::isspace(c) || c == '_' || c == '-')
                    {
                        capitalize_next = true;
                    }
                    else if (capitalize_next)
                    {
                        c = std::toupper(c);
                        capitalize_next = false;
                    }
                    else
                    {
                        c = std::tolower(c);
                    }
                }
            }
            break;

        case CaseChange::SentenceCase:
            if (!result.empty())
            {
                std::transform(result.begin(), result.end(), result.begin(), ::tolower);
                result[0] = std::toupper(result[0]);
            }
            break;

        case CaseChange::ToggleCase:
            for (char& c : result)
            {
                if (std::isupper(c))
                    c = std::tolower(c);
                else if (std::islower(c))
                    c = std::toupper(c);
            }
            break;

        case CaseChange::CamelCase:
            {
                std::string temp;
                bool capitalize_next = false;
                for (char c : result)
                {
                    if (std::isspace(c) || c == '_' || c == '-')
                    {
                        capitalize_next = true;
                    }
                    else if (capitalize_next)
                    {
                        temp += std::toupper(c);
                        capitalize_next = false;
                    }
                    else
                    {
                        temp += std::tolower(c);
                    }
                }
                result = temp;
            }
            break;

        case CaseChange::SnakeCase:
            {
                std::string temp;
                for (size_t i = 0; i < result.size(); ++i)
                {
                    char c = result[i];
                    if (std::isupper(c) && i > 0)
                    {
                        temp += '_';
                    }
                    if (std::isspace(c) || c == '-')
                    {
                        temp += '_';
                    }
                    else
                    {
                        temp += std::tolower(c);
                    }
                }
                result = temp;
            }
            break;

        case CaseChange::KebabCase:
            {
                std::string temp;
                for (size_t i = 0; i < result.size(); ++i)
                {
                    char c = result[i];
                    if (std::isupper(c) && i > 0)
                    {
                        temp += '-';
                    }
                    if (std::isspace(c) || c == '_')
                    {
                        temp += '-';
                    }
                    else
                    {
                        temp += std::tolower(c);
                    }
                }
                result = temp;
            }
            break;
        }

        return result;
    }

    std::string BatchRename::FormatNumber(int number, NumberingStyle style, int padding) const
    {
        std::ostringstream ss;

        switch (style)
        {
        case NumberingStyle::Decimal:
            ss << number;
            break;

        case NumberingStyle::DecimalPadded:
            ss << std::setw(padding) << std::setfill('0') << number;
            break;

        case NumberingStyle::RomanLower:
            return ToRoman(number, false);

        case NumberingStyle::RomanUpper:
            return ToRoman(number, true);

        case NumberingStyle::AlphaLower:
            if (number >= 1 && number <= 26)
            {
                ss << static_cast<char>('a' + number - 1);
            }
            else
            {
                ss << number;
            }
            break;

        case NumberingStyle::AlphaUpper:
            if (number >= 1 && number <= 26)
            {
                ss << static_cast<char>('A' + number - 1);
            }
            else
            {
                ss << number;
            }
            break;
        }

        return ss.str();
    }

    std::string BatchRename::ToRoman(int number, bool uppercase) const
    {
        if (number <= 0 || number > 3999)
        {
            return std::to_string(number);
        }

        struct RomanNumeral
        {
            int value;
            const char* numeral;
        };

        static const RomanNumeral numerals[] = {
            {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
            {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
            {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
        };

        std::string result;
        for (const auto& n : numerals)
        {
            while (number >= n.value)
            {
                result += n.numeral;
                number -= n.value;
            }
        }

        if (!uppercase)
        {
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        }

        return result;
    }

    std::pair<std::string, std::string> BatchRename::SplitExtension(const std::string& filename) const
    {
        auto pos = filename.rfind('.');
        if (pos == std::string::npos || pos == 0)
        {
            return {filename, ""};
        }
        return {filename.substr(0, pos), filename.substr(pos + 1)};
    }

    bool BatchRename::HasConflict(const core::Path& new_path,
                                   const std::vector<RenamePreview>& previews,
                                   size_t current_index) const
    {
        // Check if file already exists (and it's not the same file)
        if (std::filesystem::exists(new_path.Get()))
        {
            if (new_path.String() != files_[current_index].String())
            {
                return true;
            }
        }

        // Check if any other file in the batch will have the same name
        std::string new_name = new_path.Filename();
        for (size_t i = 0; i < previews.size(); ++i)
        {
            if (i != current_index && previews[i].new_name == new_name)
            {
                // Check if they're in the same directory
                if (files_[i].Parent().String() == files_[current_index].Parent().String())
                {
                    return true;
                }
            }
        }

        return false;
    }

} // namespace opacity::batch
