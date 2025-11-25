#include "opacity/search/SearchEngine.h"
#include "opacity/filesystem/FileSystemManager.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>

namespace opacity::search
{

SearchEngine::SearchEngine()
{
    core::Logger::Get()->debug("SearchEngine initialized");
}

SearchEngine::~SearchEngine()
{
    CancelSearch();
    WaitForCompletion();
    core::Logger::Get()->debug("SearchEngine destroyed");
}

void SearchEngine::StartSearch(
    const core::Path& root_path,
    const std::string& query,
    const SearchOptions& options,
    SearchResultCallback result_callback,
    SearchProgressCallback progress_callback)
{
    // Cancel any existing search
    CancelSearch();
    WaitForCompletion();

    cancel_requested_ = false;
    is_searching_ = true;

    search_thread_ = std::thread(
        &SearchEngine::SearchThread,
        this,
        root_path,
        query,
        options,
        std::move(result_callback),
        std::move(progress_callback));
}

void SearchEngine::CancelSearch()
{
    cancel_requested_ = true;
}

bool SearchEngine::IsSearching() const
{
    return is_searching_;
}

void SearchEngine::WaitForCompletion()
{
    if (search_thread_.joinable())
    {
        search_thread_.join();
    }
}

std::vector<SearchResult> SearchEngine::SearchSync(
    const core::Path& root_path,
    const std::string& query,
    const SearchOptions& options)
{
    std::vector<SearchResult> results;
    std::mutex results_mutex;

    auto callback = [&results, &results_mutex](const SearchResult& result) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(result);
    };

    size_t files_searched = 0;
    size_t matches_found = 0;
    cancel_requested_ = false;

    SearchDirectory(root_path, query, options, callback, files_searched, matches_found);

    return results;
}

bool SearchEngine::MatchPattern(
    const std::string& filename,
    const std::string& pattern,
    bool case_sensitive)
{
    if (pattern.empty())
        return true;

    std::string fname = filename;
    std::string pat = pattern;

    if (!case_sensitive)
    {
        std::transform(fname.begin(), fname.end(), fname.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(pat.begin(), pat.end(), pat.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    // Simple wildcard matching
    size_t f = 0, p = 0;
    size_t star_idx = std::string::npos;
    size_t match_idx = 0;

    while (f < fname.size())
    {
        if (p < pat.size() && (pat[p] == '?' || pat[p] == fname[f]))
        {
            ++f;
            ++p;
        }
        else if (p < pat.size() && pat[p] == '*')
        {
            star_idx = p;
            match_idx = f;
            ++p;
        }
        else if (star_idx != std::string::npos)
        {
            p = star_idx + 1;
            ++match_idx;
            f = match_idx;
        }
        else
        {
            return false;
        }
    }

    while (p < pat.size() && pat[p] == '*')
    {
        ++p;
    }

    return p == pat.size();
}

void SearchEngine::SearchThread(
    core::Path root_path,
    std::string query,
    SearchOptions options,
    SearchResultCallback result_callback,
    SearchProgressCallback progress_callback)
{
    size_t files_searched = 0;
    size_t matches_found = 0;

    core::Logger::Get()->debug("Search started: query='{}' in '{}'", query, root_path.String());

    SearchDirectory(root_path, query, options, result_callback, files_searched, matches_found);

    if (progress_callback)
    {
        progress_callback(files_searched, matches_found);
    }

    is_searching_ = false;
    core::Logger::Get()->debug("Search completed: {} files searched, {} matches found", 
                               files_searched, matches_found);
}

void SearchEngine::SearchDirectory(
    const core::Path& directory,
    const std::string& query,
    const SearchOptions& options,
    SearchResultCallback result_callback,
    size_t& files_searched,
    size_t& matches_found)
{
    if (cancel_requested_)
        return;

    if (matches_found >= options.max_results)
        return;

    filesystem::FileSystemManager fs_manager;
    filesystem::EnumerationOptions enum_options;
    enum_options.include_hidden = options.include_hidden;
    enum_options.include_system = false;

    auto contents = fs_manager.EnumerateDirectory(directory, enum_options);
    if (!contents.success)
        return;

    for (const auto& item : contents.items)
    {
        if (cancel_requested_)
            return;

        if (matches_found >= options.max_results)
            return;

        // Check if filename matches the pattern
        bool matches = MatchPattern(item.name, query, options.case_sensitive);

        // Check extension filter
        if (matches && !options.extensions.empty() && !item.is_directory)
        {
            matches = MatchesExtensionFilter(item.extension, options.extensions);
        }

        if (matches)
        {
            SearchResult result;
            result.item = item;
            
            if (result_callback)
            {
                result_callback(result);
            }
            ++matches_found;
        }

        ++files_searched;

        // Recursively search directories
        if (item.is_directory && options.recursive)
        {
            SearchDirectory(item.full_path, query, options, result_callback, 
                           files_searched, matches_found);
        }
    }
}

bool SearchEngine::MatchesExtensionFilter(
    const std::string& extension,
    const std::vector<std::string>& extensions) const
{
    if (extensions.empty())
        return true;

    std::string lower_ext = extension;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto& filter_ext : extensions)
    {
        std::string lower_filter = filter_ext;
        std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // Remove leading dot if present
        if (!lower_filter.empty() && lower_filter[0] == '.')
        {
            lower_filter = lower_filter.substr(1);
        }

        if (lower_ext == lower_filter)
            return true;
    }

    return false;
}

} // namespace opacity::search
