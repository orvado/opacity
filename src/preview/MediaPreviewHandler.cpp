#include "opacity/preview/MediaPreviewHandler.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <d3d11.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")
#endif

namespace opacity::preview
{
    using namespace opacity::core;

    class MediaPreviewHandler::Impl
    {
    public:
        ID3D11Device* device_ = nullptr;
        bool mfInitialized_ = false;

        std::unordered_set<std::string> videoExtensions_;
        std::unordered_set<std::string> audioExtensions_;

        Impl()
        {
            // Supported video extensions
            videoExtensions_ = {
                "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm",
                "m4v", "mpeg", "mpg", "3gp", "3g2", "ts", "mts",
                "m2ts", "vob", "ogv", "divx", "xvid", "asf"
            };

            // Supported audio extensions
            audioExtensions_ = {
                "mp3", "wav", "flac", "aac", "ogg", "wma", "m4a",
                "opus", "aiff", "ape", "alac", "mid", "midi", "ac3",
                "dts", "mka", "ra", "ram"
            };
        }

        ~Impl()
        {
            Shutdown();
        }

        bool Initialize()
        {
#ifdef _WIN32
            if (!mfInitialized_) {
                HRESULT hr = MFStartup(MF_VERSION);
                if (SUCCEEDED(hr)) {
                    mfInitialized_ = true;
                    Logger::Get()->info("MediaPreviewHandler: Media Foundation initialized");
                } else {
                    Logger::Get()->error("MediaPreviewHandler: Failed to initialize Media Foundation");
                    return false;
                }
            }
#endif
            return true;
        }

        void Shutdown()
        {
#ifdef _WIN32
            if (mfInitialized_) {
                MFShutdown();
                mfInitialized_ = false;
            }
#endif
        }

#ifdef _WIN32
        MediaInfo ExtractMediaInfo(const std::filesystem::path& path) const
        {
            MediaInfo info;
            
            if (!mfInitialized_) return info;

            IMFSourceReader* pReader = nullptr;
            HRESULT hr = MFCreateSourceReaderFromURL(path.wstring().c_str(), nullptr, &pReader);
            
            if (FAILED(hr) || !pReader) {
                return info;
            }

            // Get duration
            PROPVARIANT var;
            PropVariantInit(&var);
            hr = pReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, 
                                                    MF_PD_DURATION, &var);
            if (SUCCEEDED(hr)) {
                int64_t duration100ns = 0;
                PropVariantToInt64(var, &duration100ns);
                info.duration = std::chrono::milliseconds(duration100ns / 10000);
            }
            PropVariantClear(&var);

            // Enumerate streams
            DWORD streamIndex = 0;
            while (true) {
                IMFMediaType* pMediaType = nullptr;
                hr = pReader->GetNativeMediaType(streamIndex, 0, &pMediaType);
                
                if (FAILED(hr)) break;

                GUID majorType;
                hr = pMediaType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
                
                if (SUCCEEDED(hr)) {
                    if (majorType == MFMediaType_Video) {
                        info.type = MediaType::Video;
                        VideoStreamInfo videoInfo;
                        
                        // Get dimensions
                        UINT32 width = 0, height = 0;
                        MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height);
                        videoInfo.width = width;
                        videoInfo.height = height;
                        
                        // Get frame rate
                        UINT32 numerator = 0, denominator = 1;
                        MFGetAttributeRatio(pMediaType, MF_MT_FRAME_RATE, &numerator, &denominator);
                        if (denominator > 0) {
                            videoInfo.frameRate = static_cast<double>(numerator) / denominator;
                        }
                        
                        // Get codec
                        GUID subType;
                        if (SUCCEEDED(pMediaType->GetGUID(MF_MT_SUBTYPE, &subType))) {
                            videoInfo.codec = GetCodecName(subType);
                        }

                        // Get bitrate
                        UINT32 bitrate = 0;
                        if (SUCCEEDED(pMediaType->GetUINT32(MF_MT_AVG_BITRATE, &bitrate))) {
                            videoInfo.bitrate = bitrate;
                        }

                        info.videoStreams.push_back(videoInfo);
                    }
                    else if (majorType == MFMediaType_Audio) {
                        if (info.type == MediaType::Unknown) {
                            info.type = MediaType::Audio;
                        }
                        
                        AudioStreamInfo audioInfo;
                        
                        // Get channels
                        UINT32 channels = 0;
                        if (SUCCEEDED(pMediaType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels))) {
                            audioInfo.channels = channels;
                        }
                        
                        // Get sample rate
                        UINT32 sampleRate = 0;
                        if (SUCCEEDED(pMediaType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate))) {
                            audioInfo.sampleRate = sampleRate;
                        }
                        
                        // Get bits per sample
                        UINT32 bitsPerSample = 0;
                        if (SUCCEEDED(pMediaType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample))) {
                            audioInfo.bitsPerSample = bitsPerSample;
                        }

                        // Get codec
                        GUID subType;
                        if (SUCCEEDED(pMediaType->GetGUID(MF_MT_SUBTYPE, &subType))) {
                            audioInfo.codec = GetCodecName(subType);
                        }

                        // Get bitrate
                        UINT32 bitrate = 0;
                        if (SUCCEEDED(pMediaType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bitrate))) {
                            audioInfo.bitrate = bitrate * 8;  // Convert to bits
                        }

                        info.audioStreams.push_back(audioInfo);
                    }
                }

                pMediaType->Release();
                streamIndex++;
            }

            pReader->Release();

            // Get file size
            std::error_code ec;
            info.fileSize = std::filesystem::file_size(path, ec);

            // Determine container from extension
            std::string ext = path.extension().string();
            if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
            info.container = ext;

            return info;
        }

        std::string GetCodecName(const GUID& subType) const
        {
            // Video codecs
            if (subType == MFVideoFormat_H264) return "H.264";
            if (subType == MFVideoFormat_HEVC) return "HEVC/H.265";
            if (subType == MFVideoFormat_WMV3) return "WMV3";
            if (subType == MFVideoFormat_WVC1) return "VC-1";
            if (subType == MFVideoFormat_MPEG2) return "MPEG-2";
            if (subType == MFVideoFormat_MP4V) return "MPEG-4";
            if (subType == MFVideoFormat_MJPG) return "Motion JPEG";
            
            // Audio codecs
            if (subType == MFAudioFormat_AAC) return "AAC";
            if (subType == MFAudioFormat_MP3) return "MP3";
            if (subType == MFAudioFormat_PCM) return "PCM";
            if (subType == MFAudioFormat_WMAudioV8) return "WMA";
            if (subType == MFAudioFormat_WMAudioV9) return "WMA Pro";
            if (subType == MFAudioFormat_FLAC) return "FLAC";
            if (subType == MFAudioFormat_Opus) return "Opus";
            
            return "Unknown";
        }

        VideoThumbnail ExtractThumbnailAt(const std::filesystem::path& path,
                                          std::chrono::milliseconds timestamp,
                                          int maxDimension) const
        {
            VideoThumbnail thumb;
            thumb.timestamp = timestamp;

            if (!mfInitialized_) return thumb;

            IMFSourceReader* pReader = nullptr;
            HRESULT hr = MFCreateSourceReaderFromURL(path.wstring().c_str(), nullptr, &pReader);
            
            if (FAILED(hr) || !pReader) {
                return thumb;
            }

            // Configure output format to RGB32
            IMFMediaType* pMediaType = nullptr;
            hr = MFCreateMediaType(&pMediaType);
            if (SUCCEEDED(hr)) {
                pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                
                hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                   nullptr, pMediaType);
                pMediaType->Release();
            }

            if (FAILED(hr)) {
                pReader->Release();
                return thumb;
            }

            // Seek to timestamp
            LONGLONG seekTime = timestamp.count() * 10000;  // Convert to 100ns units
            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_I8;
            var.hVal.QuadPart = seekTime;
            
            hr = pReader->SetCurrentPosition(GUID_NULL, var);
            PropVariantClear(&var);

            if (FAILED(hr)) {
                pReader->Release();
                return thumb;
            }

            // Read the frame
            DWORD streamIndex, flags;
            LONGLONG frameTimestamp;
            IMFSample* pSample = nullptr;

            hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                      0, &streamIndex, &flags, &frameTimestamp, &pSample);

            if (SUCCEEDED(hr) && pSample) {
                IMFMediaBuffer* pBuffer = nullptr;
                hr = pSample->ConvertToContiguousBuffer(&pBuffer);
                
                if (SUCCEEDED(hr) && pBuffer) {
                    BYTE* pData = nullptr;
                    DWORD maxLength = 0, currentLength = 0;
                    
                    hr = pBuffer->Lock(&pData, &maxLength, &currentLength);
                    if (SUCCEEDED(hr)) {
                        // Get frame dimensions
                        IMFMediaType* pCurrentType = nullptr;
                        pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                     &pCurrentType);
                        
                        UINT32 width = 0, height = 0;
                        MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, &width, &height);
                        
                        if (pCurrentType) pCurrentType->Release();

                        // Calculate scaled dimensions
                        int scaledWidth = width;
                        int scaledHeight = height;
                        if (width > 0 && height > 0) {
                            if (static_cast<int>(width) > maxDimension || static_cast<int>(height) > maxDimension) {
                                float scale = static_cast<float>(maxDimension) / 
                                    std::max(width, height);
                                scaledWidth = static_cast<int>(width * scale);
                                scaledHeight = static_cast<int>(height * scale);
                            }
                        }

                        // Convert BGRA to RGBA and resize (simple nearest neighbor)
                        thumb.width = scaledWidth;
                        thumb.height = scaledHeight;
                        thumb.pixels.resize(scaledWidth * scaledHeight * 4);

                        float scaleX = static_cast<float>(width) / scaledWidth;
                        float scaleY = static_cast<float>(height) / scaledHeight;

                        for (int y = 0; y < scaledHeight; y++) {
                            for (int x = 0; x < scaledWidth; x++) {
                                int srcX = static_cast<int>(x * scaleX);
                                int srcY = static_cast<int>(y * scaleY);
                                int srcIdx = (srcY * width + srcX) * 4;
                                int dstIdx = (y * scaledWidth + x) * 4;

                                // BGRA to RGBA
                                thumb.pixels[dstIdx + 0] = pData[srcIdx + 2];  // R
                                thumb.pixels[dstIdx + 1] = pData[srcIdx + 1];  // G
                                thumb.pixels[dstIdx + 2] = pData[srcIdx + 0];  // B
                                thumb.pixels[dstIdx + 3] = 255;                 // A
                            }
                        }

                        pBuffer->Unlock();
                    }
                    pBuffer->Release();
                }
                pSample->Release();
            }

            pReader->Release();

            // Create texture if device available
            if (device_ && !thumb.pixels.empty()) {
                thumb.texture = CreateTexture(thumb.pixels.data(), thumb.width, thumb.height);
            }

            return thumb;
        }

        ID3D11ShaderResourceView* CreateTexture(const uint8_t* pixels, int width, int height) const
        {
            if (!device_ || !pixels || width <= 0 || height <= 0) return nullptr;

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem = pixels;
            initData.SysMemPitch = width * 4;

            ID3D11Texture2D* pTexture = nullptr;
            HRESULT hr = device_->CreateTexture2D(&desc, &initData, &pTexture);
            if (FAILED(hr)) return nullptr;

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            ID3D11ShaderResourceView* pSRV = nullptr;
            hr = device_->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
            pTexture->Release();

            return SUCCEEDED(hr) ? pSRV : nullptr;
        }
#endif
    };

    // ============== MediaPreviewHandler ==============

    MediaPreviewHandler::MediaPreviewHandler()
        : impl_(std::make_unique<Impl>())
    {}

    MediaPreviewHandler::~MediaPreviewHandler() = default;

    MediaPreviewHandler::MediaPreviewHandler(MediaPreviewHandler&&) noexcept = default;
    MediaPreviewHandler& MediaPreviewHandler::operator=(MediaPreviewHandler&&) noexcept = default;

    void MediaPreviewHandler::Initialize(ID3D11Device* device)
    {
        impl_->device_ = device;
        impl_->Initialize();
        Logger::Get()->info("MediaPreviewHandler: Initialized");
    }

    void MediaPreviewHandler::Shutdown()
    {
        impl_->Shutdown();
    }

    bool MediaPreviewHandler::CanHandle(const core::Path& path, const std::string& extension) const
    {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return impl_->videoExtensions_.count(ext) > 0 || 
               impl_->audioExtensions_.count(ext) > 0;
    }

    MediaType MediaPreviewHandler::GetMediaType(const std::string& extension) const
    {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (impl_->videoExtensions_.count(ext) > 0) return MediaType::Video;
        if (impl_->audioExtensions_.count(ext) > 0) return MediaType::Audio;
        return MediaType::Unknown;
    }

    MediaPreviewData MediaPreviewHandler::LoadPreview(const core::Path& path, int maxThumbnails) const
    {
        MediaPreviewData preview;
        
#ifdef _WIN32
        std::filesystem::path fsPath = path.Get();
        
        preview.info = impl_->ExtractMediaInfo(fsPath);
        
        if (preview.info.type == MediaType::Video && maxThumbnails > 0) {
            preview.thumbnails = ExtractThumbnails(path, maxThumbnails);
        }
        
        preview.loaded = true;
#else
        preview.errorMessage = "Media preview not supported on this platform";
#endif

        return preview;
    }

    MediaInfo MediaPreviewHandler::GetMediaInfo(const core::Path& path) const
    {
#ifdef _WIN32
        return impl_->ExtractMediaInfo(path.Get());
#else
        return MediaInfo{};
#endif
    }

    VideoThumbnail MediaPreviewHandler::ExtractThumbnail(
        const core::Path& path,
        std::chrono::milliseconds timestamp,
        int maxDimension) const
    {
#ifdef _WIN32
        return impl_->ExtractThumbnailAt(path.Get(), timestamp, maxDimension);
#else
        return VideoThumbnail{};
#endif
    }

    std::vector<VideoThumbnail> MediaPreviewHandler::ExtractThumbnails(
        const core::Path& path,
        int count,
        int maxDimension) const
    {
        std::vector<VideoThumbnail> thumbnails;
        
#ifdef _WIN32
        auto info = GetMediaInfo(path);
        if (info.type != MediaType::Video || info.duration.count() <= 0) {
            return thumbnails;
        }

        int64_t durationMs = info.duration.count();
        int64_t interval = durationMs / (count + 1);

        for (int i = 1; i <= count; i++) {
            auto timestamp = std::chrono::milliseconds(interval * i);
            auto thumb = impl_->ExtractThumbnailAt(path.Get(), timestamp, maxDimension);
            if (!thumb.pixels.empty()) {
                thumbnails.push_back(std::move(thumb));
            }
        }
#endif

        return thumbnails;
    }

    AudioWaveform MediaPreviewHandler::GenerateWaveform(
        const core::Path& path,
        int samplesPerSecond) const
    {
        AudioWaveform waveform;
        
        // Full waveform generation would require reading audio samples
        // This is a placeholder that would need FFmpeg or similar
        Logger::Get()->warn("MediaPreviewHandler: Waveform generation not implemented");
        
        return waveform;
    }

    void MediaPreviewHandler::ReleasePreview(MediaPreviewData& preview) const
    {
#ifdef _WIN32
        for (auto& thumb : preview.thumbnails) {
            if (thumb.texture) {
                thumb.texture->Release();
                thumb.texture = nullptr;
            }
        }
#endif
        preview.thumbnails.clear();
        preview.waveform.samples.clear();
        preview.loaded = false;
    }

    std::vector<std::string> MediaPreviewHandler::GetSupportedVideoExtensions() const
    {
        return std::vector<std::string>(impl_->videoExtensions_.begin(), 
                                        impl_->videoExtensions_.end());
    }

    std::vector<std::string> MediaPreviewHandler::GetSupportedAudioExtensions() const
    {
        return std::vector<std::string>(impl_->audioExtensions_.begin(), 
                                        impl_->audioExtensions_.end());
    }

    std::vector<std::string> MediaPreviewHandler::GetSupportedExtensions() const
    {
        auto exts = GetSupportedVideoExtensions();
        auto audioExts = GetSupportedAudioExtensions();
        exts.insert(exts.end(), audioExts.begin(), audioExts.end());
        return exts;
    }

    std::string MediaPreviewHandler::FormatDuration(std::chrono::milliseconds duration)
    {
        int64_t totalSeconds = duration.count() / 1000;
        int hours = static_cast<int>(totalSeconds / 3600);
        int minutes = static_cast<int>((totalSeconds % 3600) / 60);
        int seconds = static_cast<int>(totalSeconds % 60);

        std::ostringstream ss;
        if (hours > 0) {
            ss << hours << ":"
               << std::setfill('0') << std::setw(2) << minutes << ":"
               << std::setfill('0') << std::setw(2) << seconds;
        } else {
            ss << minutes << ":"
               << std::setfill('0') << std::setw(2) << seconds;
        }
        return ss.str();
    }

    std::string MediaPreviewHandler::FormatBitrate(int64_t bitrate)
    {
        std::ostringstream ss;
        if (bitrate >= 1000000) {
            ss << std::fixed << std::setprecision(1) << (bitrate / 1000000.0) << " Mbps";
        } else if (bitrate >= 1000) {
            ss << (bitrate / 1000) << " kbps";
        } else {
            ss << bitrate << " bps";
        }
        return ss.str();
    }

} // namespace opacity::preview
