#include "opacity/preview/HexPreviewHandler.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace opacity::preview
{
    using namespace opacity::core;

    class HexPreviewHandler::Impl
    {
    public:
        std::vector<MagicSignature> magicSignatures_;

        Impl()
        {
            InitializeMagicSignatures();
        }

        void InitializeMagicSignatures()
        {
            // Common file signatures
            magicSignatures_ = {
                // Images
                {{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}, 0, "PNG Image", "png"},
                {{0xFF, 0xD8, 0xFF}, 0, "JPEG Image", "jpg"},
                {{0x47, 0x49, 0x46, 0x38}, 0, "GIF Image", "gif"},
                {{0x42, 0x4D}, 0, "BMP Image", "bmp"},
                {{0x52, 0x49, 0x46, 0x46}, 0, "WebP/AVI/WAV", "webp"},
                {{0x49, 0x49, 0x2A, 0x00}, 0, "TIFF Image (LE)", "tiff"},
                {{0x4D, 0x4D, 0x00, 0x2A}, 0, "TIFF Image (BE)", "tiff"},
                
                // Documents
                {{0x25, 0x50, 0x44, 0x46}, 0, "PDF Document", "pdf"},
                {{0x50, 0x4B, 0x03, 0x04}, 0, "ZIP/Office Document", "zip"},
                {{0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1}, 0, "MS Office (OLE)", "doc"},
                {{0x7B, 0x5C, 0x72, 0x74, 0x66}, 0, "RTF Document", "rtf"},
                
                // Archives
                {{0x1F, 0x8B, 0x08}, 0, "GZIP Archive", "gz"},
                {{0x42, 0x5A, 0x68}, 0, "BZIP2 Archive", "bz2"},
                {{0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}, 0, "7-Zip Archive", "7z"},
                {{0x52, 0x61, 0x72, 0x21, 0x1A, 0x07}, 0, "RAR Archive", "rar"},
                {{0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00}, 0, "XZ Archive", "xz"},
                
                // Executables
                {{0x4D, 0x5A}, 0, "Windows Executable", "exe"},
                {{0x7F, 0x45, 0x4C, 0x46}, 0, "ELF Executable", "elf"},
                {{0xCF, 0xFA, 0xED, 0xFE}, 0, "Mach-O (32-bit)", "macho"},
                {{0xCE, 0xFA, 0xED, 0xFE}, 0, "Mach-O (64-bit)", "macho"},
                
                // Video
                {{0x00, 0x00, 0x00}, 0, "MP4/MOV Video", "mp4"},  // Partial, needs more checks
                {{0x1A, 0x45, 0xDF, 0xA3}, 0, "Matroska/WebM", "mkv"},
                {{0x46, 0x4C, 0x56, 0x01}, 0, "FLV Video", "flv"},
                
                // Audio
                {{0x49, 0x44, 0x33}, 0, "MP3 Audio (ID3)", "mp3"},
                {{0xFF, 0xFB}, 0, "MP3 Audio", "mp3"},
                {{0xFF, 0xFA}, 0, "MP3 Audio", "mp3"},
                {{0x66, 0x4C, 0x61, 0x43}, 0, "FLAC Audio", "flac"},
                {{0x4F, 0x67, 0x67, 0x53}, 0, "OGG Audio", "ogg"},
                
                // Fonts
                {{0x00, 0x01, 0x00, 0x00}, 0, "TrueType Font", "ttf"},
                {{0x4F, 0x54, 0x54, 0x4F}, 0, "OpenType Font", "otf"},
                {{0x77, 0x4F, 0x46, 0x46}, 0, "WOFF Font", "woff"},
                {{0x77, 0x4F, 0x46, 0x32}, 0, "WOFF2 Font", "woff2"},
                
                // Database
                {{0x53, 0x51, 0x4C, 0x69, 0x74, 0x65, 0x20, 0x66, 0x6F, 0x72, 0x6D, 0x61, 0x74, 0x20, 0x33}, 0, "SQLite Database", "sqlite"},
            };
        }

        std::string DetectMagic(const std::vector<uint8_t>& data) const
        {
            for (const auto& sig : magicSignatures_) {
                if (sig.offset + sig.bytes.size() <= data.size()) {
                    bool match = true;
                    for (size_t i = 0; i < sig.bytes.size(); i++) {
                        if (data[sig.offset + i] != sig.bytes[i]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        return sig.description;
                    }
                }
            }
            return "";
        }
    };

    // ============== HexPreviewHandler ==============

    HexPreviewHandler::HexPreviewHandler()
        : impl_(std::make_unique<Impl>())
    {}

    HexPreviewHandler::~HexPreviewHandler() = default;

    HexPreviewHandler::HexPreviewHandler(HexPreviewHandler&&) noexcept = default;
    HexPreviewHandler& HexPreviewHandler::operator=(HexPreviewHandler&&) noexcept = default;

    void HexPreviewHandler::Initialize()
    {
        Logger::Get()->info("HexPreviewHandler: Initialized");
    }

    bool HexPreviewHandler::CanHandle(const core::Path& path, const std::string& extension) const
    {
        // Hex preview can handle any file
        return true;
    }

    HexPreviewData HexPreviewHandler::LoadPreview(
        const core::Path& path,
        uint64_t offset,
        size_t maxBytes,
        HexDisplayFormat format) const
    {
        HexPreviewData preview;
        preview.format = format;
        preview.startOffset = offset;

        std::filesystem::path fsPath = path.Get();
        
        std::ifstream file(fsPath, std::ios::binary);
        if (!file.is_open()) {
            preview.errorMessage = "Failed to open file";
            return preview;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        preview.totalSize = static_cast<size_t>(file.tellg());
        
        if (offset >= preview.totalSize) {
            preview.errorMessage = "Offset beyond file size";
            return preview;
        }

        file.seekg(offset);

        // Read bytes
        size_t bytesToRead = std::min(maxBytes, preview.totalSize - static_cast<size_t>(offset));
        std::vector<uint8_t> buffer(bytesToRead);
        file.read(reinterpret_cast<char*>(buffer.data()), bytesToRead);
        size_t bytesRead = static_cast<size_t>(file.gcount());

        if (bytesRead == 0) {
            preview.errorMessage = "No bytes read";
            return preview;
        }

        preview.endOffset = offset + bytesRead;

        // Build hex lines
        int bytesPerRow = GetBytesPerRow(format);
        uint64_t currentOffset = offset;

        for (size_t i = 0; i < bytesRead; i += bytesPerRow) {
            HexLine line;
            line.offset = currentOffset;

            size_t lineBytes = std::min(static_cast<size_t>(bytesPerRow), bytesRead - i);
            line.bytes.assign(buffer.begin() + i, buffer.begin() + i + lineBytes);
            line.hexString = BytesToHex(line.bytes);
            line.asciiString = BytesToAscii(line.bytes);

            // Pad if not full row
            if (lineBytes < static_cast<size_t>(bytesPerRow)) {
                size_t padding = bytesPerRow - lineBytes;
                line.hexString += std::string(padding * 3, ' ');
                line.asciiString += std::string(padding, ' ');
            }

            preview.lines.push_back(std::move(line));
            currentOffset += bytesPerRow;
        }

        // Get stats from first portion of file
        preview.stats = GetBinaryStats(path, 8192);

        preview.loaded = true;
        return preview;
    }

    BinaryStats HexPreviewHandler::GetBinaryStats(const core::Path& path, size_t sampleSize) const
    {
        BinaryStats stats;

        std::filesystem::path fsPath = path.Get();
        
        std::ifstream file(fsPath, std::ios::binary);
        if (!file.is_open()) {
            return stats;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        stats.fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0);

        // Read sample
        size_t bytesToRead = std::min(sampleSize, stats.fileSize);
        std::vector<uint8_t> buffer(bytesToRead);
        file.read(reinterpret_cast<char*>(buffer.data()), bytesToRead);
        size_t bytesRead = static_cast<size_t>(file.gcount());

        if (bytesRead == 0) return stats;

        // Analyze bytes
        for (size_t i = 0; i < bytesRead; i++) {
            uint8_t b = buffer[i];
            if (b == 0) {
                stats.nullBytes++;
            } else if (b >= 32 && b < 127) {
                stats.printableBytes++;
            } else if (b < 32) {
                stats.controlBytes++;
            } else {
                stats.highBytes++;
            }
        }

        // Calculate entropy
        stats.entropy = CalculateEntropy(buffer);

        // Determine if text or binary
        double printableRatio = static_cast<double>(stats.printableBytes) / bytesRead;
        double nullRatio = static_cast<double>(stats.nullBytes) / bytesRead;

        stats.isProbablyText = printableRatio > 0.85 && nullRatio < 0.01;
        stats.isProbablyBinary = !stats.isProbablyText;

        // Detect magic signature
        stats.detectedType = impl_->DetectMagic(buffer);
        if (!stats.detectedType.empty()) {
            stats.magicSignature = BytesToHex(
                std::vector<uint8_t>(buffer.begin(), 
                                     buffer.begin() + std::min(size_t(16), buffer.size())));
        }

        return stats;
    }

    int64_t HexPreviewHandler::SearchBytes(
        const core::Path& path,
        const std::vector<uint8_t>& pattern,
        uint64_t startOffset) const
    {
        if (pattern.empty()) return -1;

        std::filesystem::path fsPath = path.Get();
        std::ifstream file(fsPath, std::ios::binary);
        if (!file.is_open()) return -1;

        file.seekg(startOffset);

        const size_t bufferSize = 65536;
        std::vector<uint8_t> buffer(bufferSize + pattern.size() - 1);
        uint64_t currentOffset = startOffset;

        while (file) {
            file.read(reinterpret_cast<char*>(buffer.data()), bufferSize);
            size_t bytesRead = static_cast<size_t>(file.gcount());
            if (bytesRead == 0) break;

            // Search in buffer
            for (size_t i = 0; i <= bytesRead - pattern.size(); i++) {
                bool match = true;
                for (size_t j = 0; j < pattern.size(); j++) {
                    if (buffer[i + j] != pattern[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return currentOffset + i;
                }
            }

            // Overlap for patterns spanning buffers
            if (bytesRead == bufferSize) {
                file.seekg(-static_cast<std::streamoff>(pattern.size() - 1), std::ios::cur);
                currentOffset += bufferSize - (pattern.size() - 1);
            } else {
                break;
            }
        }

        return -1;
    }

    int64_t HexPreviewHandler::SearchString(
        const core::Path& path,
        const std::string& pattern,
        bool caseSensitive,
        uint64_t startOffset) const
    {
        std::vector<uint8_t> bytes(pattern.begin(), pattern.end());
        
        if (!caseSensitive) {
            // For case-insensitive, we'd need a more complex algorithm
            // For now, just search for lowercase version
            std::transform(bytes.begin(), bytes.end(), bytes.begin(), ::tolower);
        }

        return SearchBytes(path, bytes, startOffset);
    }

    std::string HexPreviewHandler::DetectFileType(const core::Path& path) const
    {
        std::filesystem::path fsPath = path.Get();
        std::ifstream file(fsPath, std::ios::binary);
        if (!file.is_open()) return "";

        std::vector<uint8_t> header(64);
        file.read(reinterpret_cast<char*>(header.data()), header.size());
        
        return impl_->DetectMagic(header);
    }

    std::string HexPreviewHandler::BytesToHex(const std::vector<uint8_t>& bytes, char separator)
    {
        std::ostringstream ss;
        for (size_t i = 0; i < bytes.size(); i++) {
            if (i > 0 && separator != '\0') ss << separator;
            ss << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
               << static_cast<int>(bytes[i]);
        }
        return ss.str();
    }

    std::string HexPreviewHandler::BytesToAscii(const std::vector<uint8_t>& bytes)
    {
        std::string result;
        result.reserve(bytes.size());

        for (uint8_t b : bytes) {
            if (b >= 32 && b < 127) {
                result += static_cast<char>(b);
            } else {
                result += '.';
            }
        }

        return result;
    }

    std::string HexPreviewHandler::FormatOffset(uint64_t offset, int width)
    {
        std::ostringstream ss;
        ss << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << offset;
        return ss.str();
    }

    std::vector<uint8_t> HexPreviewHandler::HexToBytes(const std::string& hex)
    {
        std::vector<uint8_t> bytes;
        std::string cleanHex;

        // Remove spaces and other separators
        for (char c : hex) {
            if (std::isxdigit(c)) {
                cleanHex += c;
            }
        }

        // Convert pairs
        for (size_t i = 0; i + 1 < cleanHex.size(); i += 2) {
            try {
                bytes.push_back(static_cast<uint8_t>(
                    std::stoi(cleanHex.substr(i, 2), nullptr, 16)));
            } catch (...) {
                break;
            }
        }

        return bytes;
    }

    double HexPreviewHandler::CalculateEntropy(const std::vector<uint8_t>& data)
    {
        if (data.empty()) return 0.0;

        std::array<size_t, 256> freq = {};
        for (uint8_t b : data) {
            freq[b]++;
        }

        double entropy = 0.0;
        double size = static_cast<double>(data.size());

        for (size_t count : freq) {
            if (count > 0) {
                double p = count / size;
                entropy -= p * std::log2(p);
            }
        }

        return entropy;
    }

    int HexPreviewHandler::GetBytesPerRow(HexDisplayFormat format)
    {
        switch (format) {
            case HexDisplayFormat::Hex8:  return 8;
            case HexDisplayFormat::Hex16: return 16;
            case HexDisplayFormat::Hex32: return 32;
            default:                      return 16;
        }
    }

} // namespace opacity::preview
