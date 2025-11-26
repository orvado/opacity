#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "opacity/core/Path.h"

namespace opacity::preview
{
    /**
     * @brief Display format for hex view
     */
    enum class HexDisplayFormat
    {
        Hex8,       // 8 bytes per row
        Hex16,      // 16 bytes per row (default)
        Hex32       // 32 bytes per row
    };

    /**
     * @brief A single line in hex display
     */
    struct HexLine
    {
        uint64_t offset = 0;
        std::vector<uint8_t> bytes;
        std::string hexString;
        std::string asciiString;
    };

    /**
     * @brief Binary file statistics
     */
    struct BinaryStats
    {
        size_t fileSize = 0;
        size_t nullBytes = 0;
        size_t printableBytes = 0;
        size_t controlBytes = 0;
        size_t highBytes = 0;  // > 127
        double entropy = 0.0;
        bool isProbablyText = false;
        bool isProbablyBinary = false;
        std::string magicSignature;
        std::string detectedType;
    };

    /**
     * @brief Magic number signature
     */
    struct MagicSignature
    {
        std::vector<uint8_t> bytes;
        size_t offset = 0;
        std::string description;
        std::string extension;
    };

    /**
     * @brief Preview data for hex view
     */
    struct HexPreviewData
    {
        std::vector<HexLine> lines;
        BinaryStats stats;
        uint64_t startOffset = 0;
        uint64_t endOffset = 0;
        size_t totalSize = 0;
        HexDisplayFormat format = HexDisplayFormat::Hex16;
        bool loaded = false;
        std::string errorMessage;
    };

    /**
     * @brief Handler for hex preview of binary files
     */
    class HexPreviewHandler
    {
    public:
        HexPreviewHandler();
        ~HexPreviewHandler();

        // Non-copyable, movable
        HexPreviewHandler(const HexPreviewHandler&) = delete;
        HexPreviewHandler& operator=(const HexPreviewHandler&) = delete;
        HexPreviewHandler(HexPreviewHandler&&) noexcept;
        HexPreviewHandler& operator=(HexPreviewHandler&&) noexcept;

        /**
         * @brief Initialize handler
         */
        void Initialize();

        /**
         * @brief Check if this handler can preview the given file
         * Hex preview is available for any file
         */
        bool CanHandle(const core::Path& path, const std::string& extension) const;

        /**
         * @brief Load hex preview for a file
         * @param path Path to the file
         * @param offset Starting offset
         * @param maxBytes Maximum bytes to read
         * @param format Display format
         */
        HexPreviewData LoadPreview(
            const core::Path& path,
            uint64_t offset = 0,
            size_t maxBytes = 4096,
            HexDisplayFormat format = HexDisplayFormat::Hex16) const;

        /**
         * @brief Get binary file statistics
         */
        BinaryStats GetBinaryStats(const core::Path& path, size_t sampleSize = 8192) const;

        /**
         * @brief Search for bytes in file
         * @return Offset of first match, or -1 if not found
         */
        int64_t SearchBytes(
            const core::Path& path,
            const std::vector<uint8_t>& pattern,
            uint64_t startOffset = 0) const;

        /**
         * @brief Search for string in file
         * @return Offset of first match, or -1 if not found
         */
        int64_t SearchString(
            const core::Path& path,
            const std::string& pattern,
            bool caseSensitive = true,
            uint64_t startOffset = 0) const;

        /**
         * @brief Detect file type from magic number
         */
        std::string DetectFileType(const core::Path& path) const;

        /**
         * @brief Format bytes as hex string
         */
        static std::string BytesToHex(const std::vector<uint8_t>& bytes, char separator = ' ');

        /**
         * @brief Format bytes as ASCII string (non-printable as dots)
         */
        static std::string BytesToAscii(const std::vector<uint8_t>& bytes);

        /**
         * @brief Format offset as hex address
         */
        static std::string FormatOffset(uint64_t offset, int width = 8);

        /**
         * @brief Parse hex string to bytes
         */
        static std::vector<uint8_t> HexToBytes(const std::string& hex);

        /**
         * @brief Calculate Shannon entropy (0.0 - 8.0)
         */
        static double CalculateEntropy(const std::vector<uint8_t>& data);

        /**
         * @brief Get bytes per row for format
         */
        static int GetBytesPerRow(HexDisplayFormat format);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace opacity::preview
