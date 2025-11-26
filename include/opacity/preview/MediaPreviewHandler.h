#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "opacity/core/Path.h"

// Forward declare D3D11 types
struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace opacity::preview
{
    /**
     * @brief Media type enumeration
     */
    enum class MediaType
    {
        Unknown,
        Video,
        Audio
    };

    /**
     * @brief Video stream information
     */
    struct VideoStreamInfo
    {
        int width = 0;
        int height = 0;
        double frameRate = 0.0;
        std::string codec;
        std::string pixelFormat;
        int64_t bitrate = 0;
    };

    /**
     * @brief Audio stream information
     */
    struct AudioStreamInfo
    {
        int channels = 0;
        int sampleRate = 0;
        std::string codec;
        int64_t bitrate = 0;
        int bitsPerSample = 0;
    };

    /**
     * @brief Media file metadata
     */
    struct MediaInfo
    {
        MediaType type = MediaType::Unknown;
        std::chrono::milliseconds duration{0};
        size_t fileSize = 0;
        std::string container;  // e.g., "MP4", "MKV", "AVI", "MP3"
        std::string title;
        std::string artist;
        std::string album;
        std::string year;
        
        std::vector<VideoStreamInfo> videoStreams;
        std::vector<AudioStreamInfo> audioStreams;
        
        // Album art for audio files
        std::vector<uint8_t> albumArt;
        int albumArtWidth = 0;
        int albumArtHeight = 0;
    };

    /**
     * @brief Thumbnail from video
     */
    struct VideoThumbnail
    {
        std::vector<uint8_t> pixels;  // RGBA
        int width = 0;
        int height = 0;
        std::chrono::milliseconds timestamp{0};
        ID3D11ShaderResourceView* texture = nullptr;
    };

    /**
     * @brief Audio waveform data
     */
    struct AudioWaveform
    {
        std::vector<float> samples;  // Normalized -1.0 to 1.0
        int sampleRate = 0;
        int channels = 0;
        std::chrono::milliseconds duration{0};
    };

    /**
     * @brief Preview data for media files
     */
    struct MediaPreviewData
    {
        MediaInfo info;
        std::vector<VideoThumbnail> thumbnails;
        AudioWaveform waveform;
        bool loaded = false;
        std::string errorMessage;
    };

    /**
     * @brief Progress callback for media operations
     */
    using MediaProgressCallback = std::function<void(float progress, const std::string& status)>;

    /**
     * @brief Handler for previewing video and audio files
     * 
     * Note: Full video playback requires FFmpeg integration.
     * This handler provides metadata extraction and thumbnail generation
     * using Windows APIs (Media Foundation) as a fallback.
     */
    class MediaPreviewHandler
    {
    public:
        MediaPreviewHandler();
        ~MediaPreviewHandler();

        // Non-copyable, movable
        MediaPreviewHandler(const MediaPreviewHandler&) = delete;
        MediaPreviewHandler& operator=(const MediaPreviewHandler&) = delete;
        MediaPreviewHandler(MediaPreviewHandler&&) noexcept;
        MediaPreviewHandler& operator=(MediaPreviewHandler&&) noexcept;

        /**
         * @brief Initialize with D3D11 device for texture creation
         */
        void Initialize(ID3D11Device* device);

        /**
         * @brief Shutdown and release resources
         */
        void Shutdown();

        /**
         * @brief Check if this handler can preview the given file
         */
        bool CanHandle(const core::Path& path, const std::string& extension) const;

        /**
         * @brief Get media type for a file extension
         */
        MediaType GetMediaType(const std::string& extension) const;

        /**
         * @brief Load preview data for a media file
         */
        MediaPreviewData LoadPreview(const core::Path& path, int maxThumbnails = 5) const;

        /**
         * @brief Get media info without generating thumbnails
         */
        MediaInfo GetMediaInfo(const core::Path& path) const;

        /**
         * @brief Extract thumbnail at specific time
         */
        VideoThumbnail ExtractThumbnail(
            const core::Path& path,
            std::chrono::milliseconds timestamp,
            int maxDimension = 320) const;

        /**
         * @brief Extract multiple thumbnails at even intervals
         */
        std::vector<VideoThumbnail> ExtractThumbnails(
            const core::Path& path,
            int count = 5,
            int maxDimension = 160) const;

        /**
         * @brief Generate audio waveform
         */
        AudioWaveform GenerateWaveform(
            const core::Path& path,
            int samplesPerSecond = 100) const;

        /**
         * @brief Release resources for a preview
         */
        void ReleasePreview(MediaPreviewData& preview) const;

        /**
         * @brief Get supported video extensions
         */
        std::vector<std::string> GetSupportedVideoExtensions() const;

        /**
         * @brief Get supported audio extensions
         */
        std::vector<std::string> GetSupportedAudioExtensions() const;

        /**
         * @brief Get all supported extensions
         */
        std::vector<std::string> GetSupportedExtensions() const;

        /**
         * @brief Format duration as string (HH:MM:SS or MM:SS)
         */
        static std::string FormatDuration(std::chrono::milliseconds duration);

        /**
         * @brief Format bitrate as string (e.g., "192 kbps", "1.5 Mbps")
         */
        static std::string FormatBitrate(int64_t bitrate);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace opacity::preview
