#pragma once

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
     * @brief Document type enumeration
     */
    enum class DocumentType
    {
        Unknown,
        PDF,
        Word,       // .doc, .docx
        Excel,      // .xls, .xlsx
        PowerPoint, // .ppt, .pptx
        RTF,
        OpenDocument  // .odt, .ods, .odp
    };

    /**
     * @brief Document metadata
     */
    struct DocumentInfo
    {
        DocumentType type = DocumentType::Unknown;
        std::string title;
        std::string author;
        std::string subject;
        std::string creator;
        std::string creationDate;
        std::string modificationDate;
        int pageCount = 0;
        int wordCount = 0;
        int characterCount = 0;
        size_t fileSize = 0;
        bool isEncrypted = false;
        bool hasPassword = false;
    };

    /**
     * @brief Document page thumbnail
     */
    struct DocumentThumbnail
    {
        std::vector<uint8_t> pixels;  // RGBA
        int width = 0;
        int height = 0;
        int pageNumber = 0;
        ID3D11ShaderResourceView* texture = nullptr;
    };

    /**
     * @brief Extracted text content
     */
    struct DocumentTextContent
    {
        std::string text;
        int pageNumber = 0;
        bool truncated = false;
    };

    /**
     * @brief Preview data for document files
     */
    struct DocumentPreviewData
    {
        DocumentInfo info;
        std::vector<DocumentThumbnail> thumbnails;
        std::vector<DocumentTextContent> textContent;
        bool loaded = false;
        std::string errorMessage;
    };

    /**
     * @brief Handler for previewing document files (PDF, Office documents)
     * 
     * Note: Full document rendering requires external libraries:
     * - PDFium, MuPDF, or Poppler for PDF
     * - LibreOffice/OpenOffice SDK for Office documents
     * 
     * This handler provides metadata extraction and basic preview
     * capabilities using Windows APIs where available.
     */
    class DocumentPreviewHandler
    {
    public:
        DocumentPreviewHandler();
        ~DocumentPreviewHandler();

        // Non-copyable, movable
        DocumentPreviewHandler(const DocumentPreviewHandler&) = delete;
        DocumentPreviewHandler& operator=(const DocumentPreviewHandler&) = delete;
        DocumentPreviewHandler(DocumentPreviewHandler&&) noexcept;
        DocumentPreviewHandler& operator=(DocumentPreviewHandler&&) noexcept;

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
         * @brief Get document type for a file extension
         */
        DocumentType GetDocumentType(const std::string& extension) const;

        /**
         * @brief Load preview data for a document file
         */
        DocumentPreviewData LoadPreview(const core::Path& path, int maxPages = 3) const;

        /**
         * @brief Get document info without generating thumbnails
         */
        DocumentInfo GetDocumentInfo(const core::Path& path) const;

        /**
         * @brief Extract thumbnail for specific page
         */
        DocumentThumbnail ExtractPageThumbnail(
            const core::Path& path,
            int pageNumber,
            int maxDimension = 300) const;

        /**
         * @brief Extract text from document
         */
        std::string ExtractText(const core::Path& path, int maxCharacters = 10000) const;

        /**
         * @brief Release resources for a preview
         */
        void ReleasePreview(DocumentPreviewData& preview) const;

        /**
         * @brief Get supported extensions
         */
        std::vector<std::string> GetSupportedExtensions() const;

        /**
         * @brief Get document type display name
         */
        static std::string GetTypeName(DocumentType type);

        /**
         * @brief Check if document is password protected
         */
        bool IsPasswordProtected(const core::Path& path) const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace opacity::preview
