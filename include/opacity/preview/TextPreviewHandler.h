#pragma once

#include <string>
#include <vector>
#include <memory>
#include "opacity/core/Path.h"

namespace opacity::preview
{
    /**
     * @brief Preview data for text files
     */
    struct TextPreviewData
    {
        std::vector<std::string> lines;
        size_t total_lines = 0;
        std::string encoding;  // e.g., "UTF-8", "ASCII"
        bool truncated = false;
        std::string error_message;
    };

    /**
     * @brief Handler for previewing text-based files
     */
    class TextPreviewHandler
    {
    public:
        TextPreviewHandler();
        ~TextPreviewHandler();

        /**
         * @brief Check if this handler can preview the given file
         * @param path Path to the file
         * @param extension File extension (lowercase, no dot)
         * @return true if this handler supports the file
         */
        bool CanHandle(const core::Path& path, const std::string& extension) const;

        /**
         * @brief Load preview data for a text file
         * @param path Path to the file
         * @param max_lines Maximum number of lines to load (0 = no limit)
         * @param max_line_length Maximum characters per line before truncation
         * @return Preview data
         */
        TextPreviewData LoadPreview(
            const core::Path& path,
            size_t max_lines = 500,
            size_t max_line_length = 1000) const;

        /**
         * @brief Get the list of supported extensions
         */
        std::vector<std::string> GetSupportedExtensions() const;

    private:
        std::vector<std::string> supported_extensions_;
    };

} // namespace opacity::preview
