#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace opacity::ui
{
    /**
     * @brief A command that can be executed from the palette
     */
    struct PaletteCommand
    {
        std::string id;                     // Unique identifier
        std::string label;                  // Display label
        std::string description;            // Optional description
        std::string category;               // Category for grouping
        std::string shortcut;               // Keyboard shortcut display
        std::function<void()> action;       // Action to execute
        bool enabled = true;                // Is command currently available
        int priority = 0;                   // Higher = shown first in results
        std::vector<std::string> keywords;  // Additional search keywords
    };

    /**
     * @brief Match result for fuzzy search
     */
    struct PaletteMatch
    {
        const PaletteCommand* command = nullptr;
        int score = 0;                      // Match score (higher = better)
        std::vector<size_t> matched_indices;// Character indices that matched
    };

    /**
     * @brief History entry for recent commands
     */
    struct PaletteHistoryEntry
    {
        std::string command_id;
        std::chrono::system_clock::time_point timestamp;
        int use_count = 1;
    };

    /**
     * @brief Command palette for Phase 3
     * 
     * Features:
     * - Fuzzy search for all commands
     * - Keyboard-driven workflow
     * - Recent commands history
     * - Category filtering
     * - Custom command registration
     */
    class CommandPalette
    {
    public:
        CommandPalette();
        ~CommandPalette();

        // Disable copy
        CommandPalette(const CommandPalette&) = delete;
        CommandPalette& operator=(const CommandPalette&) = delete;

        /**
         * @brief Register a command
         * @param command Command to register
         */
        void RegisterCommand(const PaletteCommand& command);

        /**
         * @brief Register multiple commands
         */
        void RegisterCommands(const std::vector<PaletteCommand>& commands);

        /**
         * @brief Unregister a command by ID
         */
        void UnregisterCommand(const std::string& id);

        /**
         * @brief Clear all registered commands
         */
        void ClearCommands();

        /**
         * @brief Get a command by ID
         */
        const PaletteCommand* GetCommand(const std::string& id) const;

        /**
         * @brief Get all registered commands
         */
        std::vector<const PaletteCommand*> GetAllCommands() const;

        /**
         * @brief Get commands by category
         */
        std::vector<const PaletteCommand*> GetCommandsByCategory(const std::string& category) const;

        /**
         * @brief Get all categories
         */
        std::vector<std::string> GetCategories() const;

        /**
         * @brief Search commands with fuzzy matching
         * @param query Search query
         * @param max_results Maximum results to return (0 = all)
         * @return Sorted list of matches (best first)
         */
        std::vector<PaletteMatch> Search(const std::string& query, size_t max_results = 10) const;

        /**
         * @brief Execute a command by ID
         * @param id Command ID
         * @return true if command was found and executed
         */
        bool Execute(const std::string& id);

        /**
         * @brief Enable or disable a command
         */
        void SetCommandEnabled(const std::string& id, bool enabled);

        /**
         * @brief Get recent commands (most recent first)
         */
        std::vector<const PaletteCommand*> GetRecentCommands(size_t max_count = 10) const;

        /**
         * @brief Clear command history
         */
        void ClearHistory();

        // UI State Management
        
        /**
         * @brief Show the command palette
         */
        void Show();

        /**
         * @brief Hide the command palette
         */
        void Hide();

        /**
         * @brief Toggle palette visibility
         */
        void Toggle();

        /**
         * @brief Check if palette is visible
         */
        bool IsVisible() const { return visible_; }

        /**
         * @brief Set the search query
         */
        void SetQuery(const std::string& query);

        /**
         * @brief Get current search query
         */
        const std::string& GetQuery() const { return current_query_; }

        /**
         * @brief Get current search results
         */
        const std::vector<PaletteMatch>& GetResults() const { return current_results_; }

        /**
         * @brief Get selected result index
         */
        int GetSelectedIndex() const { return selected_index_; }

        /**
         * @brief Set selected result index
         */
        void SetSelectedIndex(int index);

        /**
         * @brief Move selection up
         */
        void SelectPrevious();

        /**
         * @brief Move selection down
         */
        void SelectNext();

        /**
         * @brief Execute selected command
         */
        bool ExecuteSelected();

        /**
         * @brief Set category filter (empty = show all)
         */
        void SetCategoryFilter(const std::string& category);

        /**
         * @brief Get current category filter
         */
        const std::string& GetCategoryFilter() const { return category_filter_; }

        /**
         * @brief Render the palette (call from UI loop)
         * @return true if palette should remain visible
         */
        bool Render();

        // Settings

        /**
         * @brief Set maximum history size
         */
        void SetMaxHistorySize(size_t size) { max_history_size_ = size; }

        /**
         * @brief Set whether to show shortcuts in results
         */
        void SetShowShortcuts(bool show) { show_shortcuts_ = show; }

        /**
         * @brief Set whether to show descriptions in results
         */
        void SetShowDescriptions(bool show) { show_descriptions_ = show; }

        /**
         * @brief Set whether to boost recent commands in search
         */
        void SetBoostRecent(bool boost) { boost_recent_ = boost; }

    private:
        /**
         * @brief Calculate fuzzy match score
         */
        int CalculateFuzzyScore(const std::string& text, const std::string& query,
                                std::vector<size_t>& matched_indices) const;

        /**
         * @brief Add command to history
         */
        void AddToHistory(const std::string& command_id);

        /**
         * @brief Update search results based on current query
         */
        void UpdateResults();

        // Commands
        std::unordered_map<std::string, PaletteCommand> commands_;
        
        // History
        std::vector<PaletteHistoryEntry> history_;
        size_t max_history_size_ = 50;

        // UI State
        bool visible_ = false;
        std::string current_query_;
        std::vector<PaletteMatch> current_results_;
        int selected_index_ = 0;
        std::string category_filter_;
        char input_buffer_[256] = {};

        // Settings
        bool show_shortcuts_ = true;
        bool show_descriptions_ = true;
        bool boost_recent_ = true;
    };

    /**
     * @brief Helper to register standard file manager commands
     */
    void RegisterStandardCommands(CommandPalette& palette);

} // namespace opacity::ui
