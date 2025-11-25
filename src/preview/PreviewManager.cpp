#include "opacity/preview/PreviewManager.h"
#include "opacity/core/Logger.h"

#include <algorithm>

namespace opacity::preview
{

PreviewManager::PreviewManager()
{
    core::Logger::Get()->debug("PreviewManager initialized");
}

PreviewManager::~PreviewManager()
{
}

void PreviewManager::Initialize(ID3D11Device* device)
{
    device_ = device;
    image_handler_.Initialize(device);
    core::Logger::Get()->debug("PreviewManager D3D11 device initialized");
}

PreviewData PreviewManager::LoadPreview(const core::Path& path)
{
    PreviewData preview;
    preview.file_path = path.String();
    preview.file_name = path.Filename();
    preview.file_extension = path.Extension();
    
    // Remove leading dot from extension
    if (!preview.file_extension.empty() && preview.file_extension[0] == '.')
    {
        preview.file_extension = preview.file_extension.substr(1);
    }

    std::string lower_ext = preview.file_extension;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Check which handler can handle this file
    if (image_handler_.CanHandle(path, lower_ext))
    {
        preview.type = PreviewType::Image;
        preview.is_loading = true;
        preview.image_preview = image_handler_.LoadPreview(path);
        preview.is_loading = false;
        
        if (!preview.image_preview.loaded)
        {
            preview.error_message = preview.image_preview.error_message;
        }
    }
    else if (text_handler_.CanHandle(path, lower_ext))
    {
        preview.type = PreviewType::Text;
        preview.is_loading = true;
        preview.text_preview = text_handler_.LoadPreview(path);
        preview.is_loading = false;
        
        if (!preview.text_preview.error_message.empty())
        {
            preview.error_message = preview.text_preview.error_message;
        }
    }
    else
    {
        preview.type = PreviewType::Unsupported;
        preview.error_message = "No preview available for this file type";
    }

    return preview;
}

void PreviewManager::ReleasePreview(PreviewData& preview)
{
    if (preview.type == PreviewType::Image)
    {
        image_handler_.ReleasePreview(preview.image_preview);
    }
    preview.text_preview.lines.clear();
    preview.type = PreviewType::None;
}

PreviewType PreviewManager::GetPreviewType(const core::Path& path) const
{
    std::string ext = path.Extension();
    if (!ext.empty() && ext[0] == '.')
    {
        ext = ext.substr(1);
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (image_handler_.CanHandle(path, ext))
        return PreviewType::Image;
    
    if (text_handler_.CanHandle(path, ext))
        return PreviewType::Text;
    
    return PreviewType::Unsupported;
}

} // namespace opacity::preview
