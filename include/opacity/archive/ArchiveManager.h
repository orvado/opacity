#pragma once

#include "opacity/core/Path.h"
#include "opacity/filesystem/FsItem.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace opacity::archive
{
    /**
     * @brief Supported archive formats
     */
    enum class ArchiveFormat
    {
        Unknown,
        Zip,
        SevenZip,
        Tar,
        TarGz,
        TarBz2,
        Rar
    };

    /**
     * @brief Compression level for archive creation
     */
    enum class CompressionLevel
    {
        Store = 0,      // No compression
        Fastest = 1,
        Fast = 3,
        Normal = 5,
        Maximum = 7,
        Ultra = 9
    };

    /**
     * @brief Entry within an archive
     */
    struct ArchiveEntry
    {
        std::string name;               // Entry name/path within archive
        std::string full_path;          // Full path in archive (for nested archives)
        uint64_t compressed_size = 0;   // Compressed size in bytes
        uint64_t uncompressed_size = 0; // Original size in bytes
        std::chrono::system_clock::time_point modified_time;
        bool is_directory = false;
        bool is_encrypted = false;
        uint32_t crc32 = 0;
        std::string compression_method;
        double compression_ratio = 0.0; // 0.0 - 1.0

        /**
         * @brief Get the parent path within the archive
         */
        std::string GetParent() const;

        /**
         * @brief Get just the filename
         */
        std::string GetFilename() const;

        /**
         * @brief Check if this entry is at the root level
         */
        bool IsRootLevel() const;
    };

    /**
     * @brief Archive metadata
     */
    struct ArchiveInfo
    {
        core::Path path;
        ArchiveFormat format = ArchiveFormat::Unknown;
        uint64_t archive_size = 0;
        uint64_t total_uncompressed_size = 0;
        size_t file_count = 0;
        size_t directory_count = 0;
        bool is_encrypted = false;
        bool is_multi_volume = false;
        std::string comment;
        std::chrono::system_clock::time_point created_time;
    };

    /**
     * @brief Options for archive extraction
     */
    struct ExtractOptions
    {
        core::Path destination;
        bool preserve_paths = true;         // Maintain directory structure
        bool overwrite_existing = false;
        bool skip_existing = false;
        std::string password;               // For encrypted archives
        std::vector<std::string> files;     // Specific files to extract (empty = all)
    };

    /**
     * @brief Options for archive creation
     */
    struct CreateOptions
    {
        ArchiveFormat format = ArchiveFormat::Zip;
        CompressionLevel level = CompressionLevel::Normal;
        bool include_root_folder = false;   // Include base folder name
        bool recursive = true;
        std::string password;               // For encryption
        std::string comment;
        std::vector<std::string> exclude_patterns;
    };

    /**
     * @brief Progress information for archive operations
     */
    struct ArchiveProgress
    {
        size_t files_processed = 0;
        size_t total_files = 0;
        uint64_t bytes_processed = 0;
        uint64_t total_bytes = 0;
        std::string current_file;
        double percentage = 0.0;
    };

    using ArchiveProgressCallback = std::function<void(const ArchiveProgress&)>;
    using ArchiveCompleteCallback = std::function<void(bool success, const std::string& error)>;

    /**
     * @brief Result of an archive operation
     */
    struct ArchiveResult
    {
        bool success = false;
        std::string error_message;
        size_t files_processed = 0;
        uint64_t bytes_processed = 0;
        std::vector<std::string> failed_files;
    };

    /**
     * @brief Archive manager for Phase 3
     * 
     * Features:
     * - ZIP file browsing as virtual folders
     * - Archive creation from selection
     * - Extract with path preservation
     * - Archive content preview
     * - Multiple format support (via miniz for ZIP)
     */
    class ArchiveManager
    {
    public:
        ArchiveManager();
        ~ArchiveManager();

        // Disable copy
        ArchiveManager(const ArchiveManager&) = delete;
        ArchiveManager& operator=(const ArchiveManager&) = delete;

        /**
         * @brief Check if a file is a supported archive
         * @param path Path to check
         * @return true if the file is a recognized archive format
         */
        static bool IsArchive(const core::Path& path);

        /**
         * @brief Get archive format from path
         */
        static ArchiveFormat GetFormat(const core::Path& path);

        /**
         * @brief Get file extension for archive format
         */
        static std::string GetExtension(ArchiveFormat format);

        /**
         * @brief Get archive information and metadata
         * @param path Path to archive
         * @return Archive info, or empty on error
         */
        ArchiveInfo GetArchiveInfo(const core::Path& path);

        /**
         * @brief List all entries in an archive
         * @param path Path to archive
         * @param password Optional password for encrypted archives
         * @return Vector of archive entries
         */
        std::vector<ArchiveEntry> ListContents(
            const core::Path& path,
            const std::string& password = "");

        /**
         * @brief List entries at a specific path within archive (for virtual folder browsing)
         * @param archive_path Path to archive
         * @param internal_path Path within the archive (empty for root)
         * @param password Optional password
         * @return Vector of entries at the specified level
         */
        std::vector<ArchiveEntry> ListDirectory(
            const core::Path& archive_path,
            const std::string& internal_path = "",
            const std::string& password = "");

        /**
         * @brief Convert archive entries to FsItems for UI display
         */
        std::vector<filesystem::FsItem> EntriesToFsItems(
            const std::vector<ArchiveEntry>& entries,
            const core::Path& archive_path);

        /**
         * @brief Extract entire archive
         * @param archive_path Path to archive
         * @param options Extraction options
         * @param progress_callback Optional progress callback
         * @return Extraction result
         */
        ArchiveResult Extract(
            const core::Path& archive_path,
            const ExtractOptions& options,
            ArchiveProgressCallback progress_callback = nullptr);

        /**
         * @brief Extract a single file from archive
         * @param archive_path Path to archive
         * @param entry_name Name/path of entry to extract
         * @param destination Destination path
         * @param password Optional password
         * @return true on success
         */
        bool ExtractFile(
            const core::Path& archive_path,
            const std::string& entry_name,
            const core::Path& destination,
            const std::string& password = "");

        /**
         * @brief Extract file to memory buffer
         * @param archive_path Path to archive
         * @param entry_name Name/path of entry
         * @param password Optional password
         * @return File contents, or empty on error
         */
        std::vector<uint8_t> ExtractToMemory(
            const core::Path& archive_path,
            const std::string& entry_name,
            const std::string& password = "");

        /**
         * @brief Create a new archive
         * @param archive_path Path for new archive
         * @param source_paths Files/folders to add
         * @param options Creation options
         * @param progress_callback Optional progress callback
         * @return Creation result
         */
        ArchiveResult Create(
            const core::Path& archive_path,
            const std::vector<core::Path>& source_paths,
            const CreateOptions& options = CreateOptions{},
            ArchiveProgressCallback progress_callback = nullptr);

        /**
         * @brief Add files to existing archive
         * @param archive_path Path to archive
         * @param source_paths Files to add
         * @param base_path Base path for relative paths in archive
         * @param progress_callback Optional progress callback
         * @return Result
         */
        ArchiveResult AddFiles(
            const core::Path& archive_path,
            const std::vector<core::Path>& source_paths,
            const core::Path& base_path,
            ArchiveProgressCallback progress_callback = nullptr);

        /**
         * @brief Delete files from archive
         * @param archive_path Path to archive
         * @param entry_names Names of entries to delete
         * @return Result
         */
        ArchiveResult DeleteFiles(
            const core::Path& archive_path,
            const std::vector<std::string>& entry_names);

        /**
         * @brief Test archive integrity
         * @param archive_path Path to archive
         * @param password Optional password
         * @return true if archive is valid
         */
        bool TestArchive(
            const core::Path& archive_path,
            const std::string& password = "");

        /**
         * @brief Cancel ongoing operation
         */
        void Cancel();

        /**
         * @brief Check if operation is in progress
         */
        bool IsRunning() const { return running_.load(); }

        /**
         * @brief Get last error message
         */
        std::string GetLastError() const { return last_error_; }

    private:
        /**
         * @brief Collect files for archiving (recursive)
         */
        std::vector<std::pair<core::Path, std::string>> CollectFiles(
            const core::Path& source,
            const core::Path& base_path,
            const CreateOptions& options);

        /**
         * @brief Check if path matches exclude patterns
         */
        bool ShouldExclude(const std::string& path, const std::vector<std::string>& patterns) const;

        std::atomic<bool> running_{false};
        std::atomic<bool> cancel_requested_{false};
        std::string last_error_;
        mutable std::mutex mutex_;
    };

} // namespace opacity::archive
