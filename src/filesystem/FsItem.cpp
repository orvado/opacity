#include "opacity/filesystem/FsItem.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cctype>
#include <unordered_map>
#include <regex>

namespace opacity::filesystem
{

// ============================================================================
// FsItem Implementation
// ============================================================================

std::string FsItem::GetFormattedSize() const
{
    if (is_directory)
    {
        return "";  // Don't show size for directories
    }

    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double display_size = static_cast<double>(size);
    int unit_index = 0;

    while (display_size >= 1024.0 && unit_index < 5)
    {
        display_size /= 1024.0;
        unit_index++;
    }

    std::ostringstream oss;
    if (unit_index == 0)
    {
        oss << size << " " << units[unit_index];
    }
    else
    {
        oss << std::fixed << std::setprecision(1) << display_size << " " << units[unit_index];
    }
    return oss.str();
}

std::string FsItem::GetFormattedModifiedDate() const
{
    auto time_t_val = std::chrono::system_clock::to_time_t(modified);
    std::tm tm_val;
    localtime_s(&tm_val, &time_t_val);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%d %H:%M");
    return oss.str();
}

std::string FsItem::GetFormattedCreatedDate() const
{
    auto time_t_val = std::chrono::system_clock::to_time_t(created);
    std::tm tm_val;
    localtime_s(&tm_val, &time_t_val);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%d %H:%M");
    return oss.str();
}

std::string FsItem::GetTypeDescription() const
{
    if (is_directory)
    {
        return "Folder";
    }

    if (extension.empty())
    {
        return "File";
    }

    // Common file type descriptions
    static const std::unordered_map<std::string, std::string> type_descriptions = {
        // Text files
        {"txt", "Text Document"},
        {"md", "Markdown Document"},
        {"log", "Log File"},
        {"csv", "CSV File"},
        {"json", "JSON File"},
        {"xml", "XML File"},
        {"yaml", "YAML File"},
        {"yml", "YAML File"},
        
        // Code files
        {"cpp", "C++ Source File"},
        {"c", "C Source File"},
        {"h", "C/C++ Header"},
        {"hpp", "C++ Header"},
        {"cs", "C# Source File"},
        {"py", "Python Script"},
        {"js", "JavaScript File"},
        {"ts", "TypeScript File"},
        {"java", "Java Source File"},
        {"rs", "Rust Source File"},
        {"go", "Go Source File"},
        {"rb", "Ruby Script"},
        {"php", "PHP File"},
        {"html", "HTML Document"},
        {"htm", "HTML Document"},
        {"css", "CSS Stylesheet"},
        {"sql", "SQL Script"},
        
        // Image files
        {"jpg", "JPEG Image"},
        {"jpeg", "JPEG Image"},
        {"png", "PNG Image"},
        {"gif", "GIF Image"},
        {"bmp", "Bitmap Image"},
        {"ico", "Icon File"},
        {"svg", "SVG Image"},
        {"webp", "WebP Image"},
        {"tiff", "TIFF Image"},
        {"tif", "TIFF Image"},
        {"psd", "Photoshop Document"},
        
        // Audio files
        {"mp3", "MP3 Audio"},
        {"wav", "WAV Audio"},
        {"flac", "FLAC Audio"},
        {"ogg", "OGG Audio"},
        {"m4a", "M4A Audio"},
        {"wma", "WMA Audio"},
        
        // Video files
        {"mp4", "MP4 Video"},
        {"mkv", "MKV Video"},
        {"avi", "AVI Video"},
        {"mov", "QuickTime Video"},
        {"wmv", "WMV Video"},
        {"webm", "WebM Video"},
        
        // Document files
        {"pdf", "PDF Document"},
        {"doc", "Word Document"},
        {"docx", "Word Document"},
        {"xls", "Excel Spreadsheet"},
        {"xlsx", "Excel Spreadsheet"},
        {"ppt", "PowerPoint Presentation"},
        {"pptx", "PowerPoint Presentation"},
        {"odt", "OpenDocument Text"},
        {"ods", "OpenDocument Spreadsheet"},
        
        // Archive files
        {"zip", "ZIP Archive"},
        {"rar", "RAR Archive"},
        {"7z", "7-Zip Archive"},
        {"tar", "TAR Archive"},
        {"gz", "GZip Archive"},
        {"bz2", "BZip2 Archive"},
        
        // Executable files
        {"exe", "Application"},
        {"dll", "Dynamic Link Library"},
        {"msi", "Windows Installer"},
        {"bat", "Batch File"},
        {"cmd", "Command Script"},
        {"ps1", "PowerShell Script"},
        {"sh", "Shell Script"},
        
        // Config files
        {"ini", "Configuration File"},
        {"cfg", "Configuration File"},
        {"conf", "Configuration File"},
        {"reg", "Registry File"},
        
        // Other
        {"iso", "Disc Image"},
        {"img", "Disk Image"},
        {"vhd", "Virtual Hard Disk"},
        {"vmdk", "VMware Disk"},
    };

    auto it = type_descriptions.find(extension);
    if (it != type_descriptions.end())
    {
        return it->second;
    }

    // Default: uppercase extension + "File"
    std::string upper_ext = extension;
    for (char& c : upper_ext)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return upper_ext + " File";
}

// ============================================================================
// FsItemComparator Implementation
// ============================================================================

FsItemComparator::FsItemComparator(SortColumn column, SortDirection direction, bool folders_first)
    : column_(column)
    , direction_(direction)
    , folders_first_(folders_first)
{
}

bool FsItemComparator::operator()(const FsItem& a, const FsItem& b) const
{
    // Folders first (or last if folders_first_ is false)
    if (folders_first_ && a.is_directory != b.is_directory)
    {
        return a.is_directory > b.is_directory;  // true > false, so directories come first
    }

    int cmp = 0;
    
    switch (column_)
    {
    case SortColumn::Name:
        // Case-insensitive comparison
        {
            std::string a_lower = a.name;
            std::string b_lower = b.name;
            for (char& c : a_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : b_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            cmp = a_lower.compare(b_lower);
        }
        break;
        
    case SortColumn::Size:
        if (a.size < b.size) cmp = -1;
        else if (a.size > b.size) cmp = 1;
        else cmp = 0;
        break;
        
    case SortColumn::Type:
        {
            std::string a_type = a.GetTypeDescription();
            std::string b_type = b.GetTypeDescription();
            cmp = a_type.compare(b_type);
        }
        break;
        
    case SortColumn::DateModified:
        if (a.modified < b.modified) cmp = -1;
        else if (a.modified > b.modified) cmp = 1;
        else cmp = 0;
        break;
        
    case SortColumn::DateCreated:
        if (a.created < b.created) cmp = -1;
        else if (a.created > b.created) cmp = 1;
        else cmp = 0;
        break;
    }

    // Apply direction
    if (direction_ == SortDirection::Descending)
    {
        cmp = -cmp;
    }

    return cmp < 0;
}

// ============================================================================
// FsItemUtils Implementation
// ============================================================================

namespace FsItemUtils
{

void Sort(std::vector<FsItem>& items, const FsItemComparator& comparator)
{
    std::sort(items.begin(), items.end(), comparator);
}

std::vector<FsItem> FilterByName(const std::vector<FsItem>& items, const std::string& pattern)
{
    if (pattern.empty() || pattern == "*")
    {
        return items;
    }

    // Convert wildcard pattern to regex
    std::string regex_pattern;
    for (char c : pattern)
    {
        switch (c)
        {
        case '*':
            regex_pattern += ".*";
            break;
        case '?':
            regex_pattern += ".";
            break;
        case '.':
        case '+':
        case '^':
        case '$':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '|':
        case '\\':
            regex_pattern += '\\';
            regex_pattern += c;
            break;
        default:
            regex_pattern += c;
            break;
        }
    }

    std::vector<FsItem> result;
    try
    {
        std::regex re(regex_pattern, std::regex::icase);
        for (const auto& item : items)
        {
            if (std::regex_match(item.name, re))
            {
                result.push_back(item);
            }
        }
    }
    catch (const std::regex_error&)
    {
        // Invalid pattern, return all items
        return items;
    }

    return result;
}

std::vector<FsItem> FilterByExtension(const std::vector<FsItem>& items, const std::string& extension)
{
    if (extension.empty())
    {
        return items;
    }

    std::string lower_ext = extension;
    for (char& c : lower_ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Remove leading dot if present
    if (!lower_ext.empty() && lower_ext[0] == '.')
    {
        lower_ext = lower_ext.substr(1);
    }

    std::vector<FsItem> result;
    for (const auto& item : items)
    {
        if (item.extension == lower_ext)
        {
            result.push_back(item);
        }
    }
    return result;
}

std::vector<FsItem> FilterHidden(const std::vector<FsItem>& items, bool show_hidden)
{
    if (show_hidden)
    {
        return items;
    }

    std::vector<FsItem> result;
    for (const auto& item : items)
    {
        if (!item.IsHidden())
        {
            result.push_back(item);
        }
    }
    return result;
}

std::string GetExtension(const std::string& filename)
{
    size_t dot_pos = filename.rfind('.');
    if (dot_pos == std::string::npos || dot_pos == 0 || dot_pos == filename.length() - 1)
    {
        return "";
    }

    std::string ext = filename.substr(dot_pos + 1);
    for (char& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

std::string GetMimeType(const std::string& extension)
{
    static const std::unordered_map<std::string, std::string> mime_types = {
        // Text
        {"txt", "text/plain"},
        {"html", "text/html"},
        {"htm", "text/html"},
        {"css", "text/css"},
        {"js", "text/javascript"},
        {"json", "application/json"},
        {"xml", "application/xml"},
        {"csv", "text/csv"},
        {"md", "text/markdown"},
        
        // Code
        {"cpp", "text/x-c++src"},
        {"c", "text/x-csrc"},
        {"h", "text/x-chdr"},
        {"hpp", "text/x-c++hdr"},
        {"py", "text/x-python"},
        {"java", "text/x-java-source"},
        {"cs", "text/x-csharp"},
        {"ts", "text/typescript"},
        {"rs", "text/x-rust"},
        {"go", "text/x-go"},
        
        // Images
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"png", "image/png"},
        {"gif", "image/gif"},
        {"bmp", "image/bmp"},
        {"svg", "image/svg+xml"},
        {"ico", "image/x-icon"},
        {"webp", "image/webp"},
        
        // Audio
        {"mp3", "audio/mpeg"},
        {"wav", "audio/wav"},
        {"ogg", "audio/ogg"},
        {"flac", "audio/flac"},
        
        // Video
        {"mp4", "video/mp4"},
        {"webm", "video/webm"},
        {"mkv", "video/x-matroska"},
        {"avi", "video/x-msvideo"},
        {"mov", "video/quicktime"},
        
        // Documents
        {"pdf", "application/pdf"},
        {"doc", "application/msword"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xls", "application/vnd.ms-excel"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        
        // Archives
        {"zip", "application/zip"},
        {"rar", "application/vnd.rar"},
        {"7z", "application/x-7z-compressed"},
        {"tar", "application/x-tar"},
        {"gz", "application/gzip"},
        
        // Executables
        {"exe", "application/vnd.microsoft.portable-executable"},
        {"dll", "application/vnd.microsoft.portable-executable"},
        {"msi", "application/x-msi"},
    };

    auto it = mime_types.find(extension);
    if (it != mime_types.end())
    {
        return it->second;
    }
    return "application/octet-stream";
}

} // namespace FsItemUtils

} // namespace opacity::filesystem
