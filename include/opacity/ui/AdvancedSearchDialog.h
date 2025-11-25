#pragma once

#include "opacity/search/SearchEngine.h"
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <functional>

namespace opacity::ui
{
    /**
     * @brief Size comparison operators
     */
    enum class SizeComparison
    {
        Any,
        LessThan,
        GreaterThan,
        Between,
        Equals
    };

    /**
     * @brief Date comparison operators
     */
    enum class DateComparison
    {
        Any,
        Before,
        After,
        Between,
        Today,
        Yesterday,
        ThisWeek,
        ThisMonth,
        ThisYear
    };

    /**
     * @brief Size units
     */
    enum class SizeUnit
    {
        Bytes,
        KB,
        MB,
        GB
    };

    /**
     * @brief Advanced search criteria
     */
    struct AdvancedSearchCriteria
    {
        // Basic search
        std::string name_pattern;
        bool name_case_sensitive = false;
        bool name_use_regex = false;
        bool name_whole_word = false;

        // Content search
        bool search_contents = false;
        std::string content_pattern;
        bool content_case_sensitive = false;
        bool content_use_regex = false;

        // File type filters
        std::vector<std::string> include_extensions;
        std::vector<std::string> exclude_extensions;
        bool include_directories = true;
        bool include_files = true;
        bool include_hidden = false;
        bool include_system = false;

        // Size filter
        SizeComparison size_comparison = SizeComparison::Any;
        uint64_t size_min = 0;
        uint64_t size_max = 0;
        SizeUnit size_unit = SizeUnit::KB;

        // Date filter (modified date)
        DateComparison date_comparison = DateComparison::Any;
        std::chrono::system_clock::time_point date_min;
        std::chrono::system_clock::time_point date_max;

        // Attributes filter
        bool filter_readonly = false;
        bool filter_archive = false;
        bool filter_compressed = false;
        bool filter_encrypted = false;

        // Scope
        std::string search_path;
        bool recursive = true;
        int max_depth = -1;  // -1 = unlimited

        // Result options
        size_t max_results = 10000;
    };

    /**
     * @brief Saved search configuration
     */
    struct SavedSearch
    {
        std::string name;
        std::string description;
        AdvancedSearchCriteria criteria;
        std::chrono::system_clock::time_point last_used;
    };

    /**
     * @brief Advanced search dialog for Phase 2
     * 
     * Features:
     * - Multiple search criteria
     * - Date/size filters
     * - Regex support
     * - Saved searches
     * - Search result management
     */
    class AdvancedSearchDialog
    {
    public:
        using SearchStartCallback = std::function<void(const AdvancedSearchCriteria&)>;
        using SearchResultCallback = std::function<void(const search::SearchResult&)>;
        using SearchCompleteCallback = std::function<void(size_t total_results)>;

        AdvancedSearchDialog();
        ~AdvancedSearchDialog();

        /**
         * @brief Show the dialog
         */
        void Show();

        /**
         * @brief Hide the dialog
         */
        void Hide();

        /**
         * @brief Check if dialog is visible
         */
        bool IsVisible() const { return visible_; }

        /**
         * @brief Set the initial search path
         */
        void SetSearchPath(const std::string& path);

        /**
         * @brief Get current search criteria
         */
        const AdvancedSearchCriteria& GetCriteria() const { return criteria_; }

        /**
         * @brief Set search criteria
         */
        void SetCriteria(const AdvancedSearchCriteria& criteria) { criteria_ = criteria; }

        /**
         * @brief Render the dialog
         * @return true if search was started
         */
        bool Render();

        // Callbacks
        void SetSearchStartCallback(SearchStartCallback cb) { on_search_start_ = std::move(cb); }
        void SetSearchResultCallback(SearchResultCallback cb) { on_result_ = std::move(cb); }
        void SetSearchCompleteCallback(SearchCompleteCallback cb) { on_complete_ = std::move(cb); }

        // Saved searches
        void SaveCurrentSearch(const std::string& name);
        void LoadSavedSearch(const std::string& name);
        void DeleteSavedSearch(const std::string& name);
        std::vector<SavedSearch> GetSavedSearches() const;

        /**
         * @brief Load saved searches from file
         */
        bool LoadSearches(const std::string& path);

        /**
         * @brief Save searches to file
         */
        bool SaveSearches(const std::string& path) const;

    private:
        void RenderBasicTab();
        void RenderFiltersTab();
        void RenderSavedSearchesTab();
        void RenderResultsPanel();

        void ResetCriteria();
        uint64_t GetSizeInBytes(uint64_t value, SizeUnit unit) const;

        bool visible_ = false;
        AdvancedSearchCriteria criteria_;
        std::vector<SavedSearch> saved_searches_;

        // UI state
        char name_buffer_[512] = "";
        char content_buffer_[512] = "";
        char path_buffer_[512] = "";
        char extensions_include_[256] = "";
        char extensions_exclude_[256] = "";
        int size_min_input_ = 0;
        int size_max_input_ = 0;
        int current_tab_ = 0;

        // Results
        std::vector<search::SearchResult> results_;
        bool searching_ = false;
        size_t results_count_ = 0;

        // Callbacks
        SearchStartCallback on_search_start_;
        SearchResultCallback on_result_;
        SearchCompleteCallback on_complete_;
    };

} // namespace opacity::ui
