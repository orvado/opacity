#pragma once

#include "opacity/core/Path.h"

#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace opacity::batch
{
    /**
     * @brief Types of rename operations
     */
    enum class RenameOperation
    {
        Replace,            // Find and replace text
        RegexReplace,       // Regex-based replace
        AddPrefix,          // Add text at start
        AddSuffix,          // Add text before extension
        RemoveCharacters,   // Remove specific characters/patterns
        ChangeCase,         // Change letter case
        InsertText,         // Insert text at position
        TrimText,           // Remove text from start/end
        Numbering,          // Add sequential numbers
        DateFormat,         // Add/replace with date
        Extension           // Change file extension
    };

    /**
     * @brief Case change options
     */
    enum class CaseChange
    {
        Lowercase,
        Uppercase,
        TitleCase,
        SentenceCase,
        ToggleCase,
        CamelCase,
        SnakeCase,
        KebabCase
    };

    /**
     * @brief Numbering style options
     */
    enum class NumberingStyle
    {
        Decimal,            // 1, 2, 3...
        DecimalPadded,      // 001, 002, 003...
        RomanLower,         // i, ii, iii...
        RomanUpper,         // I, II, III...
        AlphaLower,         // a, b, c...
        AlphaUpper          // A, B, C...
    };

    /**
     * @brief Position for text insertion/numbering
     */
    enum class InsertPosition
    {
        Start,              // Before filename
        End,                // After filename (before extension)
        AtIndex,            // At specific character index
        BeforeText,         // Before specific text
        AfterText,          // After specific text
        ReplaceFilename     // Replace entire filename
    };

    /**
     * @brief A single rename rule
     */
    struct RenameRule
    {
        RenameOperation operation = RenameOperation::Replace;
        std::string find_text;
        std::string replace_text;
        
        // Case options
        CaseChange case_change = CaseChange::Lowercase;
        bool apply_to_extension = false;
        
        // Numbering options
        NumberingStyle number_style = NumberingStyle::Decimal;
        int start_number = 1;
        int increment = 1;
        int padding = 3;
        std::string number_prefix;
        std::string number_suffix;
        
        // Position options
        InsertPosition position = InsertPosition::End;
        int insert_index = 0;
        
        // Regex options
        bool case_sensitive = false;
        bool match_whole_name = false;
        bool use_regex = false;
        
        // Trim options
        int trim_start = 0;
        int trim_end = 0;
    };

    /**
     * @brief Preview of a single rename operation
     */
    struct RenamePreview
    {
        core::Path original_path;
        std::string original_name;
        std::string new_name;
        bool has_conflict = false;          // New name conflicts with existing file
        bool has_error = false;
        std::string error_message;
        bool will_change = false;           // Name actually changes
    };

    /**
     * @brief Result of a rename operation
     */
    struct RenameResult
    {
        size_t total_files = 0;
        size_t renamed_count = 0;
        size_t skipped_count = 0;
        size_t error_count = 0;
        std::vector<std::string> errors;
        bool success = false;
    };

    /**
     * @brief Progress callback data
     */
    struct RenameProgress
    {
        size_t files_processed = 0;
        size_t total_files = 0;
        std::string current_file;
        double percentage = 0.0;
    };

    using RenameProgressCallback = std::function<void(const RenameProgress&)>;

    /**
     * @brief Batch rename manager for Phase 3
     * 
     * Features:
     * - Pattern-based renaming with preview
     * - Numbering with custom padding
     * - Regex support for complex patterns
     * - Multiple chained rules
     * - Undo support
     */
    class BatchRename
    {
    public:
        BatchRename();
        ~BatchRename();

        // Disable copy
        BatchRename(const BatchRename&) = delete;
        BatchRename& operator=(const BatchRename&) = delete;

        /**
         * @brief Add files to the rename queue
         */
        void AddFiles(const std::vector<core::Path>& paths);

        /**
         * @brief Clear all files from the queue
         */
        void ClearFiles();

        /**
         * @brief Get current file list
         */
        const std::vector<core::Path>& GetFiles() const { return files_; }

        /**
         * @brief Add a rename rule
         */
        void AddRule(const RenameRule& rule);

        /**
         * @brief Remove a rule by index
         */
        void RemoveRule(size_t index);

        /**
         * @brief Clear all rules
         */
        void ClearRules();

        /**
         * @brief Get current rules
         */
        const std::vector<RenameRule>& GetRules() const { return rules_; }

        /**
         * @brief Move rule up in order
         */
        void MoveRuleUp(size_t index);

        /**
         * @brief Move rule down in order
         */
        void MoveRuleDown(size_t index);

        /**
         * @brief Generate preview of all renames
         * @return Vector of previews for each file
         */
        std::vector<RenamePreview> GeneratePreview();

        /**
         * @brief Apply a single rule to a filename
         * @param filename The original filename (without path)
         * @param rule The rule to apply
         * @param file_index Index of file (for numbering)
         * @return The modified filename
         */
        std::string ApplyRule(const std::string& filename, 
                             const RenameRule& rule, 
                             size_t file_index);

        /**
         * @brief Apply all rules to a filename
         * @param filename The original filename
         * @param file_index Index for numbering
         * @return The final filename
         */
        std::string ApplyAllRules(const std::string& filename, size_t file_index);

        /**
         * @brief Execute the rename operations
         * @param progress_callback Optional progress callback
         * @return Result of the operation
         */
        RenameResult Execute(RenameProgressCallback progress_callback = nullptr);

        /**
         * @brief Undo the last rename operation
         * @return true if undo was successful
         */
        bool Undo();

        /**
         * @brief Check if undo is available
         */
        bool CanUndo() const { return !undo_stack_.empty(); }

        /**
         * @brief Sort files by name
         */
        void SortByName(bool ascending = true);

        /**
         * @brief Sort files by date
         */
        void SortByDate(bool ascending = true);

        /**
         * @brief Sort files by size
         */
        void SortBySize(bool ascending = true);

        /**
         * @brief Randomize file order
         */
        void Randomize();

        /**
         * @brief Reverse file order
         */
        void Reverse();

        // Common rule presets
        static RenameRule CreateReplaceRule(const std::string& find, 
                                            const std::string& replace,
                                            bool case_sensitive = false);
        
        static RenameRule CreateRegexRule(const std::string& pattern,
                                         const std::string& replace);
        
        static RenameRule CreateNumberingRule(int start = 1,
                                             int padding = 3,
                                             InsertPosition pos = InsertPosition::Start);
        
        static RenameRule CreateCaseRule(CaseChange case_type,
                                        bool include_extension = false);
        
        static RenameRule CreateExtensionRule(const std::string& new_extension);

    private:
        /**
         * @brief Apply case change to text
         */
        std::string ApplyCaseChange(const std::string& text, CaseChange change) const;

        /**
         * @brief Format number according to style
         */
        std::string FormatNumber(int number, NumberingStyle style, int padding) const;

        /**
         * @brief Convert to Roman numerals
         */
        std::string ToRoman(int number, bool uppercase) const;

        /**
         * @brief Split filename and extension
         */
        std::pair<std::string, std::string> SplitExtension(const std::string& filename) const;

        /**
         * @brief Check if new name conflicts with existing files
         */
        bool HasConflict(const core::Path& new_path, 
                        const std::vector<RenamePreview>& previews,
                        size_t current_index) const;

        std::vector<core::Path> files_;
        std::vector<RenameRule> rules_;
        
        // Undo stack: pairs of (new_path, original_path)
        std::vector<std::vector<std::pair<core::Path, core::Path>>> undo_stack_;
        static constexpr size_t MAX_UNDO_LEVELS = 10;
    };

} // namespace opacity::batch
