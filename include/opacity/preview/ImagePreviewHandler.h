#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "opacity/core/Path.h"

// Forward declare D3D11 types
struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace opacity::preview
{
    /**
     * @brief Image metadata
     */
    struct ImageInfo
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        std::string format;  // e.g., "PNG", "JPEG", "BMP"
        size_t file_size = 0;
    };

    /**
     * @brief Preview data for image files
     */
    struct ImagePreviewData
    {
        ImageInfo info;
        std::vector<uint8_t> pixels;  // RGBA pixel data
        ID3D11ShaderResourceView* texture = nullptr;  // GPU texture for ImGui rendering
        bool loaded = false;
        std::string error_message;
    };

    /**
     * @brief Handler for previewing image files
     */
    class ImagePreviewHandler
    {
    public:
        ImagePreviewHandler();
        ~ImagePreviewHandler();

        /**
         * @brief Initialize with D3D11 device for texture creation
         * @param device D3D11 device
         */
        void Initialize(ID3D11Device* device);

        /**
         * @brief Check if this handler can preview the given file
         * @param path Path to the file
         * @param extension File extension (lowercase, no dot)
         * @return true if this handler supports the file
         */
        bool CanHandle(const core::Path& path, const std::string& extension) const;

        /**
         * @brief Load preview data for an image file
         * @param path Path to the file
         * @param max_dimension Maximum dimension (width or height) for thumbnail
         * @return Preview data
         */
        ImagePreviewData LoadPreview(
            const core::Path& path,
            int max_dimension = 512) const;

        /**
         * @brief Release resources for a preview
         */
        void ReleasePreview(ImagePreviewData& preview) const;

        /**
         * @brief Get the list of supported extensions
         */
        std::vector<std::string> GetSupportedExtensions() const;

        /**
         * @brief Get image info without loading pixels
         */
        ImageInfo GetImageInfo(const core::Path& path) const;

    private:
        ID3D11ShaderResourceView* CreateTexture(
            const uint8_t* pixels,
            int width,
            int height) const;

        std::vector<std::string> supported_extensions_;
        ID3D11Device* device_ = nullptr;
    };

} // namespace opacity::preview
