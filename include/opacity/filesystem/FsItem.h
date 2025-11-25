#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <algorithm>
#include "opacity/core/Path.h"

namespace opacity::filesystem
{
    using core::Path;

    /**
     * @brief File type classification
     */
    enum class FileType
    {
        Unknown,
        Directory,
        Text,
        Image,
        Audio,
        Video,
        Archive,
        Document,
        Executable,
        Code,
        Data
    };

    /**
     * @brief Determine file type from filename/extension
     */
    FileType DetermineFileType(const std::string& filename);

    /**
     * @brief Represents a filesystem item (file or folder)
     */
    struct FsItem
    {
        std::string name;
        std::string path;           // String path for compatibility
        Path full_path;
        bool is_directory = false;
        bool is_symlink = false;
        uint64_t size = 0;
        std::chrono::system_clock::time_point modified_time;
        std::chrono::system_clock::time_point modified;     // Alias
        std::chrono::system_clock::time_point created;
        uint32_t attributes = 0;
        std::string extension;
        std::string mime_type;
        FileType type = FileType::Unknown;

        // Windows file attribute constants
        static constexpr uint32_t ATTR_READONLY = 0x1;
        static constexpr uint32_t ATTR_HIDDEN = 0x2;
        static constexpr uint32_t ATTR_SYSTEM = 0x4;
        static constexpr uint32_t ATTR_DIRECTORY = 0x10;
        static constexpr uint32_t ATTR_ARCHIVE = 0x20;
        static constexpr uint32_t ATTR_COMPRESSED = 0x800;
        static constexpr uint32_t ATTR_ENCRYPTED = 0x4000;

        bool IsReadOnly() const { return (attributes & ATTR_READONLY) != 0; }
        bool IsHidden() const { return (attributes & ATTR_HIDDEN) != 0; }
        bool IsSystem() const { return (attributes & ATTR_SYSTEM) != 0; }
        bool IsArchive() const { return (attributes & ATTR_ARCHIVE) != 0; }
        bool IsCompressed() const { return (attributes & ATTR_COMPRESSED) != 0; }
        bool IsEncrypted() const { return (attributes & ATTR_ENCRYPTED) != 0; }

        // Get formatted size string (e.g., "1.5 MB", "256 KB")
        std::string GetFormattedSize() const;
        
        // Get formatted date string (e.g., "2024-01-15 14:30")
        std::string GetFormattedModifiedDate() const;
        
        // Get formatted created date string
        std::string GetFormattedCreatedDate() const;
        
        // Get file type description (e.g., "Text Document", "JPEG Image", "Folder")
        std::string GetTypeDescription() const;
    };

    /**
     * @brief Sorting options for file items
     */
    enum class SortColumn
    {
        Name,
        Size,
        Type,
        DateModified,
        DateCreated
    };

    enum class SortDirection
    {
        Ascending,
        Descending
    };

    /**
     * @brief Comparator for sorting FsItems
     */
    class FsItemComparator
    {
    public:
        FsItemComparator(SortColumn column = SortColumn::Name, 
                         SortDirection direction = SortDirection::Ascending,
                         bool folders_first = true);

        bool operator()(const FsItem& a, const FsItem& b) const;

        void SetColumn(SortColumn column) { column_ = column; }
        void SetDirection(SortDirection direction) { direction_ = direction; }
        void SetFoldersFirst(bool folders_first) { folders_first_ = folders_first; }
        
        SortColumn GetColumn() const { return column_; }
        SortDirection GetDirection() const { return direction_; }
        bool GetFoldersFirst() const { return folders_first_; }

    private:
        SortColumn column_;
        SortDirection direction_;
        bool folders_first_;
    };

    /**
     * @brief Helper functions for file items
     */
    namespace FsItemUtils
    {
        // Sort a vector of FsItems
        void Sort(std::vector<FsItem>& items, const FsItemComparator& comparator);
        
        // Filter items by name pattern (supports * and ? wildcards)
        std::vector<FsItem> FilterByName(const std::vector<FsItem>& items, const std::string& pattern);
        
        // Filter items by extension
        std::vector<FsItem> FilterByExtension(const std::vector<FsItem>& items, const std::string& extension);
        
        // Filter to show/hide hidden files
        std::vector<FsItem> FilterHidden(const std::vector<FsItem>& items, bool show_hidden);
        
        // Get extension from filename (lowercase, without dot)
        std::string GetExtension(const std::string& filename);
        
        // Get mime type from extension
        std::string GetMimeType(const std::string& extension);
    }

} // namespace opacity::filesystem
