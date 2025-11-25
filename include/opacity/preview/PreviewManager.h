#pragma once

#include <memory>
#include <string>
#include <variant>
#include "opacity/preview/TextPreviewHandler.h"
#include "opacity/preview/ImagePreviewHandler.h"
#include "opacity/core/Path.h"

// Forward declare D3D11 types
struct ID3D11Device;

namespace opacity::preview
{
    /**
     * @brief Type of preview content
     */
    enum class PreviewType
    {
        None,
        Text,
        Image,
        Unsupported
    };

    /**
     * @brief Unified preview data container
     */
    struct PreviewData
    {
        PreviewType type = PreviewType::None;
        std::string file_path;
        std::string file_name;
        std::string file_extension;
        
        // Preview content (one of these will be populated)
        TextPreviewData text_preview;
        ImagePreviewData image_preview;
        
        // General info
        std::string error_message;
        bool is_loading = false;
    };

    /**
     * @brief Manages preview handlers and coordinates file previews
     */
    class PreviewManager
    {
    public:
        PreviewManager();
        ~PreviewManager();

        /**
         * @brief Initialize with D3D11 device for image textures
         */
        void Initialize(ID3D11Device* device);

        /**
         * @brief Load preview for a file
         * @param path Path to the file
         * @return Preview data
         */
        PreviewData LoadPreview(const core::Path& path);

        /**
         * @brief Release resources from a preview
         */
        void ReleasePreview(PreviewData& preview);

        /**
         * @brief Check what type of preview is available for a file
         */
        PreviewType GetPreviewType(const core::Path& path) const;

        /**
         * @brief Get reference to text handler for direct use
         */
        TextPreviewHandler& GetTextHandler() { return text_handler_; }

        /**
         * @brief Get reference to image handler for direct use
         */
        ImagePreviewHandler& GetImageHandler() { return image_handler_; }

    private:
        TextPreviewHandler text_handler_;
        ImagePreviewHandler image_handler_;
        ID3D11Device* device_ = nullptr;
    };

} // namespace opacity::preview
