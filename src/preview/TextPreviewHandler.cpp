#include "opacity/preview/TextPreviewHandler.h"
#include "opacity/core/Logger.h"

#include <fstream>
#include <algorithm>

namespace opacity::preview
{

TextPreviewHandler::TextPreviewHandler()
{
    // Initialize list of supported text file extensions
    supported_extensions_ = {
        // Plain text
        "txt", "text", "log", "md", "markdown", "rst", "rtf",
        
        // Code files
        "c", "cpp", "cc", "cxx", "h", "hpp", "hxx", "inl",
        "cs", "java", "kt", "scala", "groovy",
        "py", "pyw", "pyx", "pxd",
        "js", "jsx", "ts", "tsx", "mjs", "cjs",
        "rb", "rake", "gemspec",
        "php", "phtml",
        "go", "rs", "swift", "m", "mm",
        "lua", "pl", "pm", "tcl", "r", "R",
        "sh", "bash", "zsh", "fish", "ps1", "psm1", "bat", "cmd",
        "asm", "s", "S",
        
        // Web files
        "html", "htm", "xhtml", "css", "scss", "sass", "less",
        "xml", "xsl", "xslt", "svg",
        "vue", "svelte",
        
        // Data files
        "json", "jsonc", "json5",
        "yaml", "yml",
        "toml", "ini", "cfg", "conf", "config",
        "csv", "tsv",
        "sql", "sqlite",
        
        // Build/config files
        "cmake", "make", "makefile", "dockerfile",
        "gradle", "maven", "pom",
        "gitignore", "gitattributes", "gitmodules",
        "editorconfig", "eslintrc", "prettierrc",
        
        // Documentation
        "readme", "license", "changelog", "contributing", "authors",
        "todo", "note", "notes"
    };
    
    core::Logger::Get()->debug("TextPreviewHandler initialized with {} supported extensions", 
                               supported_extensions_.size());
}

TextPreviewHandler::~TextPreviewHandler()
{
}

bool TextPreviewHandler::CanHandle(const core::Path& path, const std::string& extension) const
{
    if (extension.empty())
    {
        // Check if it's a common extensionless text file
        std::string filename = path.Filename();
        std::string lower_filename = filename;
        std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        
        // Common extensionless text files
        static const std::vector<std::string> extensionless_files = {
            "makefile", "dockerfile", "vagrantfile", "gemfile", "rakefile",
            "readme", "license", "changelog", "authors", "contributing",
            "todo", "notes"
        };
        
        for (const auto& name : extensionless_files)
        {
            if (lower_filename == name)
                return true;
        }
        return false;
    }

    std::string lower_ext = extension;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return std::find(supported_extensions_.begin(), supported_extensions_.end(), lower_ext) 
           != supported_extensions_.end();
}

TextPreviewData TextPreviewHandler::LoadPreview(
    const core::Path& path,
    size_t max_lines,
    size_t max_line_length) const
{
    TextPreviewData data;
    data.encoding = "UTF-8";  // Assume UTF-8 for now

    std::ifstream file(path.String(), std::ios::binary);
    if (!file.is_open())
    {
        data.error_message = "Failed to open file";
        return data;
    }

    // Read file content
    std::string line;
    while (std::getline(file, line))
    {
        ++data.total_lines;

        if (max_lines > 0 && data.lines.size() >= max_lines)
        {
            data.truncated = true;
            continue;  // Continue counting total lines
        }

        // Remove trailing carriage return (Windows line endings)
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // Truncate long lines
        if (max_line_length > 0 && line.length() > max_line_length)
        {
            line = line.substr(0, max_line_length) + "...";
        }

        // Replace tabs with spaces for display
        std::string processed_line;
        processed_line.reserve(line.size());
        for (char c : line)
        {
            if (c == '\t')
            {
                processed_line += "    ";  // 4 spaces per tab
            }
            else
            {
                processed_line += c;
            }
        }

        data.lines.push_back(std::move(processed_line));
    }

    if (file.bad())
    {
        data.error_message = "Error reading file";
    }

    return data;
}

std::vector<std::string> TextPreviewHandler::GetSupportedExtensions() const
{
    return supported_extensions_;
}

} // namespace opacity::preview
