#include "opacity/preview/DocumentPreviewHandler.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <regex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <thumbcache.h>
#include <d3d11.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace opacity::preview
{
    using namespace opacity::core;

    class DocumentPreviewHandler::Impl
    {
    public:
        ID3D11Device* device_ = nullptr;

        std::unordered_map<std::string, DocumentType> extensionMap_;
        std::unordered_set<std::string> supportedExtensions_;

        Impl()
        {
            // Map extensions to document types
            extensionMap_ = {
                {"pdf", DocumentType::PDF},
                {"doc", DocumentType::Word},
                {"docx", DocumentType::Word},
                {"docm", DocumentType::Word},
                {"xls", DocumentType::Excel},
                {"xlsx", DocumentType::Excel},
                {"xlsm", DocumentType::Excel},
                {"ppt", DocumentType::PowerPoint},
                {"pptx", DocumentType::PowerPoint},
                {"pptm", DocumentType::PowerPoint},
                {"rtf", DocumentType::RTF},
                {"odt", DocumentType::OpenDocument},
                {"ods", DocumentType::OpenDocument},
                {"odp", DocumentType::OpenDocument}
            };

            for (const auto& [ext, type] : extensionMap_) {
                supportedExtensions_.insert(ext);
            }
        }

        ~Impl() = default;

        DocumentInfo ExtractPdfInfo(const std::filesystem::path& path) const
        {
            DocumentInfo info;
            info.type = DocumentType::PDF;

            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return info;
            }

            // Read PDF header and basic info
            std::string content(8192, '\0');
            file.read(&content[0], content.size());
            file.seekg(0, std::ios::end);
            info.fileSize = static_cast<size_t>(file.tellg());

            // Check for encryption
            if (content.find("/Encrypt") != std::string::npos) {
                info.isEncrypted = true;
                info.hasPassword = true;
            }

            // Try to find page count
            std::regex pageCountRegex("/Count\\s+(\\d+)");
            std::smatch match;
            if (std::regex_search(content, match, pageCountRegex)) {
                try {
                    info.pageCount = std::stoi(match[1].str());
                } catch (...) {}
            }

            // Extract metadata from Info dictionary
            ExtractPdfMetadata(content, info);

            return info;
        }

        void ExtractPdfMetadata(const std::string& content, DocumentInfo& info) const
        {
            // Title
            std::regex titleRegex("/Title\\s*\\(([^)]+)\\)");
            std::smatch match;
            if (std::regex_search(content, match, titleRegex)) {
                info.title = DecodePdfString(match[1].str());
            }

            // Author
            std::regex authorRegex("/Author\\s*\\(([^)]+)\\)");
            if (std::regex_search(content, match, authorRegex)) {
                info.author = DecodePdfString(match[1].str());
            }

            // Subject
            std::regex subjectRegex("/Subject\\s*\\(([^)]+)\\)");
            if (std::regex_search(content, match, subjectRegex)) {
                info.subject = DecodePdfString(match[1].str());
            }

            // Creator
            std::regex creatorRegex("/Creator\\s*\\(([^)]+)\\)");
            if (std::regex_search(content, match, creatorRegex)) {
                info.creator = DecodePdfString(match[1].str());
            }

            // Creation date
            std::regex creationDateRegex("/CreationDate\\s*\\(([^)]+)\\)");
            if (std::regex_search(content, match, creationDateRegex)) {
                info.creationDate = ParsePdfDate(match[1].str());
            }

            // Modification date
            std::regex modDateRegex("/ModDate\\s*\\(([^)]+)\\)");
            if (std::regex_search(content, match, modDateRegex)) {
                info.modificationDate = ParsePdfDate(match[1].str());
            }
        }

        std::string DecodePdfString(const std::string& str) const
        {
            // Basic PDF string decoding (handles common escape sequences)
            std::string result;
            result.reserve(str.size());

            for (size_t i = 0; i < str.size(); i++) {
                if (str[i] == '\\' && i + 1 < str.size()) {
                    switch (str[i + 1]) {
                        case 'n': result += '\n'; i++; break;
                        case 'r': result += '\r'; i++; break;
                        case 't': result += '\t'; i++; break;
                        case '\\': result += '\\'; i++; break;
                        case '(': result += '('; i++; break;
                        case ')': result += ')'; i++; break;
                        default: result += str[i]; break;
                    }
                } else {
                    result += str[i];
                }
            }

            return result;
        }

        std::string ParsePdfDate(const std::string& dateStr) const
        {
            // PDF date format: D:YYYYMMDDHHmmSS+HH'mm'
            if (dateStr.size() < 3 || dateStr.substr(0, 2) != "D:") {
                return dateStr;
            }

            std::string date = dateStr.substr(2);
            if (date.size() >= 8) {
                std::string year = date.substr(0, 4);
                std::string month = date.substr(4, 2);
                std::string day = date.substr(6, 2);
                return year + "-" + month + "-" + day;
            }

            return dateStr;
        }

        DocumentInfo ExtractOfficeInfo(const std::filesystem::path& path) const
        {
            DocumentInfo info;
            
            std::string ext = path.extension().string();
            if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            auto it = extensionMap_.find(ext);
            if (it != extensionMap_.end()) {
                info.type = it->second;
            }

            std::error_code ec;
            info.fileSize = std::filesystem::file_size(path, ec);

            // Modern Office formats (.docx, .xlsx, .pptx) are ZIP archives
            // containing XML files. For simplicity, we just read basic info.
            if (ext == "docx" || ext == "xlsx" || ext == "pptx" ||
                ext == "docm" || ext == "xlsm" || ext == "pptm") {
                ExtractOoxmlInfo(path, info);
            }

            return info;
        }

        void ExtractOoxmlInfo(const std::filesystem::path& path, DocumentInfo& info) const
        {
            // OOXML files are ZIP archives - would need miniz to extract
            // For now, we just note this is a modern Office format
            std::string ext = path.extension().string();
            if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
            
            if (ext.find("doc") != std::string::npos) {
                info.type = DocumentType::Word;
            } else if (ext.find("xls") != std::string::npos) {
                info.type = DocumentType::Excel;
            } else if (ext.find("ppt") != std::string::npos) {
                info.type = DocumentType::PowerPoint;
            }
        }

        std::string ExtractPdfText(const std::filesystem::path& path, int maxChars) const
        {
            // Basic PDF text extraction - finds text between parentheses
            // Real PDF text extraction requires a proper library
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return "";
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();

            std::string extractedText;
            extractedText.reserve(maxChars);

            // Find text in Tj and TJ operators
            std::regex textRegex("\\(([^)]+)\\)\\s*Tj");
            auto begin = std::sregex_iterator(content.begin(), content.end(), textRegex);
            auto end = std::sregex_iterator();

            for (auto it = begin; it != end && static_cast<int>(extractedText.size()) < maxChars; ++it) {
                extractedText += DecodePdfString((*it)[1].str());
                extractedText += " ";
            }

            if (static_cast<int>(extractedText.size()) > maxChars) {
                extractedText.resize(maxChars);
            }

            return extractedText;
        }

#ifdef _WIN32
        DocumentThumbnail ExtractWindowsThumbnail(const std::filesystem::path& path, 
                                                   int maxDimension) const
        {
            DocumentThumbnail thumb;
            thumb.pageNumber = 1;

            // Use Windows Shell to extract thumbnail
            IShellItem* pItem = nullptr;
            HRESULT hr = SHCreateItemFromParsingName(path.wstring().c_str(), nullptr,
                                                      IID_PPV_ARGS(&pItem));
            if (FAILED(hr) || !pItem) {
                return thumb;
            }

            IThumbnailCache* pCache = nullptr;
            hr = CoCreateInstance(CLSID_LocalThumbnailCache, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pCache));
            
            if (SUCCEEDED(hr) && pCache) {
                ISharedBitmap* pBitmap = nullptr;
                WTS_CACHEFLAGS flags;
                WTS_THUMBNAILID thumbId;

                hr = pCache->GetThumbnail(pItem, maxDimension, 
                                          WTS_EXTRACT | WTS_SCALETOREQUESTEDSIZE,
                                          &pBitmap, &flags, &thumbId);

                if (SUCCEEDED(hr) && pBitmap) {
                    HBITMAP hBitmap = nullptr;
                    hr = pBitmap->GetSharedBitmap(&hBitmap);

                    if (SUCCEEDED(hr) && hBitmap) {
                        // Convert HBITMAP to RGBA pixels
                        BITMAP bm;
                        GetObject(hBitmap, sizeof(bm), &bm);

                        thumb.width = bm.bmWidth;
                        thumb.height = bm.bmHeight;
                        thumb.pixels.resize(thumb.width * thumb.height * 4);

                        BITMAPINFO bmi = {};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = thumb.width;
                        bmi.bmiHeader.biHeight = -thumb.height;  // Top-down
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;

                        HDC hdc = GetDC(nullptr);
                        std::vector<uint8_t> bgraBits(thumb.width * thumb.height * 4);
                        
                        GetDIBits(hdc, hBitmap, 0, thumb.height, bgraBits.data(), 
                                  &bmi, DIB_RGB_COLORS);
                        ReleaseDC(nullptr, hdc);

                        // Convert BGRA to RGBA
                        for (int i = 0; i < thumb.width * thumb.height; i++) {
                            thumb.pixels[i * 4 + 0] = bgraBits[i * 4 + 2];  // R
                            thumb.pixels[i * 4 + 1] = bgraBits[i * 4 + 1];  // G
                            thumb.pixels[i * 4 + 2] = bgraBits[i * 4 + 0];  // B
                            thumb.pixels[i * 4 + 3] = 255;                   // A
                        }

                        // Create D3D texture
                        if (device_ && !thumb.pixels.empty()) {
                            thumb.texture = CreateTexture(thumb.pixels.data(), 
                                                         thumb.width, thumb.height);
                        }
                    }
                    pBitmap->Release();
                }
                pCache->Release();
            }
            pItem->Release();

            return thumb;
        }

        ID3D11ShaderResourceView* CreateTexture(const uint8_t* pixels, 
                                                 int width, int height) const
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

    // ============== DocumentPreviewHandler ==============

    DocumentPreviewHandler::DocumentPreviewHandler()
        : impl_(std::make_unique<Impl>())
    {}

    DocumentPreviewHandler::~DocumentPreviewHandler() = default;

    DocumentPreviewHandler::DocumentPreviewHandler(DocumentPreviewHandler&&) noexcept = default;
    DocumentPreviewHandler& DocumentPreviewHandler::operator=(DocumentPreviewHandler&&) noexcept = default;

    void DocumentPreviewHandler::Initialize(ID3D11Device* device)
    {
        impl_->device_ = device;
        Logger::Get()->info("DocumentPreviewHandler: Initialized");
    }

    void DocumentPreviewHandler::Shutdown()
    {
        impl_->device_ = nullptr;
    }

    bool DocumentPreviewHandler::CanHandle(const core::Path& path, const std::string& extension) const
    {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return impl_->supportedExtensions_.count(ext) > 0;
    }

    DocumentType DocumentPreviewHandler::GetDocumentType(const std::string& extension) const
    {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        auto it = impl_->extensionMap_.find(ext);
        return (it != impl_->extensionMap_.end()) ? it->second : DocumentType::Unknown;
    }

    DocumentPreviewData DocumentPreviewHandler::LoadPreview(const core::Path& path, int maxPages) const
    {
        DocumentPreviewData preview;
        
        std::filesystem::path fsPath = path.Get();
        std::string ext = fsPath.extension().string();
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Get document info
        if (ext == "pdf") {
            preview.info = impl_->ExtractPdfInfo(fsPath);
        } else {
            preview.info = impl_->ExtractOfficeInfo(fsPath);
        }

#ifdef _WIN32
        // Try to get thumbnail from Windows Shell
        if (maxPages > 0) {
            auto thumb = impl_->ExtractWindowsThumbnail(fsPath, 300);
            if (!thumb.pixels.empty()) {
                preview.thumbnails.push_back(std::move(thumb));
            }
        }
#endif

        // Extract text preview for PDFs
        if (ext == "pdf") {
            DocumentTextContent textContent;
            textContent.pageNumber = 1;
            textContent.text = impl_->ExtractPdfText(fsPath, 2000);
            textContent.truncated = textContent.text.size() >= 2000;
            if (!textContent.text.empty()) {
                preview.textContent.push_back(std::move(textContent));
            }
        }

        preview.loaded = true;
        return preview;
    }

    DocumentInfo DocumentPreviewHandler::GetDocumentInfo(const core::Path& path) const
    {
        std::filesystem::path fsPath = path.Get();
        std::string ext = fsPath.extension().string();
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == "pdf") {
            return impl_->ExtractPdfInfo(fsPath);
        }
        return impl_->ExtractOfficeInfo(fsPath);
    }

    DocumentThumbnail DocumentPreviewHandler::ExtractPageThumbnail(
        const core::Path& path,
        int pageNumber,
        int maxDimension) const
    {
#ifdef _WIN32
        return impl_->ExtractWindowsThumbnail(path.Get(), maxDimension);
#else
        return DocumentThumbnail{};
#endif
    }

    std::string DocumentPreviewHandler::ExtractText(const core::Path& path, int maxCharacters) const
    {
        std::filesystem::path fsPath = path.Get();
        std::string ext = fsPath.extension().string();
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == "pdf") {
            return impl_->ExtractPdfText(fsPath, maxCharacters);
        }

        // For Office documents, we'd need to extract from OOXML
        return "";
    }

    void DocumentPreviewHandler::ReleasePreview(DocumentPreviewData& preview) const
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
        preview.textContent.clear();
        preview.loaded = false;
    }

    std::vector<std::string> DocumentPreviewHandler::GetSupportedExtensions() const
    {
        return std::vector<std::string>(impl_->supportedExtensions_.begin(),
                                        impl_->supportedExtensions_.end());
    }

    std::string DocumentPreviewHandler::GetTypeName(DocumentType type)
    {
        switch (type) {
            case DocumentType::PDF:          return "PDF Document";
            case DocumentType::Word:         return "Word Document";
            case DocumentType::Excel:        return "Excel Spreadsheet";
            case DocumentType::PowerPoint:   return "PowerPoint Presentation";
            case DocumentType::RTF:          return "Rich Text Document";
            case DocumentType::OpenDocument: return "OpenDocument";
            default:                         return "Unknown Document";
        }
    }

    bool DocumentPreviewHandler::IsPasswordProtected(const core::Path& path) const
    {
        auto info = GetDocumentInfo(path);
        return info.hasPassword;
    }

} // namespace opacity::preview
