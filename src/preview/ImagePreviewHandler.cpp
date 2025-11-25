#include "opacity/preview/ImagePreviewHandler.h"
#include "opacity/core/Logger.h"

#define NOMINMAX
#include <d3d11.h>

// stb_image for image loading
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include <stb_image.h>

#include <algorithm>
#include <fstream>

namespace opacity::preview
{

ImagePreviewHandler::ImagePreviewHandler()
{
    // Initialize list of supported image extensions
    supported_extensions_ = {
        "png", "jpg", "jpeg", "bmp", "gif", "tga", "psd", "hdr", "pic", "pnm"
    };
    
    core::Logger::Get()->debug("ImagePreviewHandler initialized with {} supported extensions", 
                               supported_extensions_.size());
}

ImagePreviewHandler::~ImagePreviewHandler()
{
}

void ImagePreviewHandler::Initialize(ID3D11Device* device)
{
    device_ = device;
    core::Logger::Get()->debug("ImagePreviewHandler D3D11 device set");
}

bool ImagePreviewHandler::CanHandle(const core::Path& path, const std::string& extension) const
{
    if (extension.empty())
        return false;

    std::string lower_ext = extension;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return std::find(supported_extensions_.begin(), supported_extensions_.end(), lower_ext) 
           != supported_extensions_.end();
}

ImageInfo ImagePreviewHandler::GetImageInfo(const core::Path& path) const
{
    ImageInfo info;
    
    // Get file size
    std::ifstream file(path.String(), std::ios::binary | std::ios::ate);
    if (file.is_open())
    {
        info.file_size = static_cast<size_t>(file.tellg());
        file.close();
    }

    // Get image dimensions using stbi_info
    int width, height, channels;
    if (stbi_info(path.String().c_str(), &width, &height, &channels))
    {
        info.width = width;
        info.height = height;
        info.channels = channels;

        // Determine format from extension
        std::string ext = path.Extension();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        
        if (ext == "JPG" || ext == "JPEG")
            info.format = "JPEG";
        else if (ext == "PNG")
            info.format = "PNG";
        else if (ext == "BMP")
            info.format = "BMP";
        else if (ext == "GIF")
            info.format = "GIF";
        else if (ext == "TGA")
            info.format = "TGA";
        else if (ext == "HDR")
            info.format = "HDR";
        else
            info.format = ext;
    }

    return info;
}

ImagePreviewData ImagePreviewHandler::LoadPreview(
    const core::Path& path,
    int max_dimension) const
{
    ImagePreviewData data;

    // Load image using stb_image
    int width, height, channels;
    unsigned char* pixels = stbi_load(path.String().c_str(), &width, &height, &channels, 4);  // Force RGBA
    
    if (!pixels)
    {
        data.error_message = std::string("Failed to load image: ") + stbi_failure_reason();
        return data;
    }

    data.info.width = width;
    data.info.height = height;
    data.info.channels = channels;

    // Resize if needed
    int display_width = width;
    int display_height = height;
    
    if (max_dimension > 0 && (width > max_dimension || height > max_dimension))
    {
        float scale = static_cast<float>(max_dimension) / static_cast<float>(std::max(width, height));
        display_width = static_cast<int>(width * scale);
        display_height = static_cast<int>(height * scale);
    }

    // Store pixel data
    size_t data_size = static_cast<size_t>(width) * height * 4;
    data.pixels.resize(data_size);
    std::memcpy(data.pixels.data(), pixels, data_size);

    // Create D3D11 texture if device is available
    if (device_)
    {
        data.texture = CreateTexture(pixels, width, height);
    }

    stbi_image_free(pixels);

    // Get additional info
    std::ifstream file(path.String(), std::ios::binary | std::ios::ate);
    if (file.is_open())
    {
        data.info.file_size = static_cast<size_t>(file.tellg());
    }

    data.loaded = true;
    return data;
}

void ImagePreviewHandler::ReleasePreview(ImagePreviewData& preview) const
{
    if (preview.texture)
    {
        preview.texture->Release();
        preview.texture = nullptr;
    }
    preview.pixels.clear();
    preview.loaded = false;
}

std::vector<std::string> ImagePreviewHandler::GetSupportedExtensions() const
{
    return supported_extensions_;
}

ID3D11ShaderResourceView* ImagePreviewHandler::CreateTexture(
    const uint8_t* pixels,
    int width,
    int height) const
{
    if (!device_ || !pixels)
        return nullptr;

    // Create texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = pixels;
    init_data.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device_->CreateTexture2D(&desc, &init_data, &texture);
    if (FAILED(hr))
    {
        core::Logger::Get()->warn("Failed to create texture2D: {}", hr);
        return nullptr;
    }

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device_->CreateShaderResourceView(texture, &srv_desc, &srv);
    texture->Release();

    if (FAILED(hr))
    {
        core::Logger::Get()->warn("Failed to create shader resource view: {}", hr);
        return nullptr;
    }

    return srv;
}

} // namespace opacity::preview
