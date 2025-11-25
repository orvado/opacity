#include "opacity/ui/Theme.h"
#include "opacity/core/Logger.h"

#include <imgui.h>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace opacity::ui
{
    Theme::Theme()
    {
        current_scheme_ = GetDarkScheme();
    }

    void Theme::Initialize()
    {
        ApplyTheme(ThemeType::Dark);
        SPDLOG_INFO("Theme system initialized");
    }

    void Theme::ApplyTheme(ThemeType type)
    {
        current_theme_ = type;

        switch (type)
        {
        case ThemeType::Light:
            ApplyLightTheme();
            break;
        case ThemeType::Dark:
            ApplyDarkTheme();
            break;
        case ThemeType::HighContrast:
            ApplyHighContrastTheme();
            break;
        case ThemeType::Custom:
            // Keep current scheme, just reapply
            ApplyImGuiColors();
            break;
        }

        if (on_theme_changed_)
            on_theme_changed_(type);
    }

    void Theme::ApplyLightTheme()
    {
        current_scheme_ = GetLightScheme();
        ApplyImGuiColors();
        SPDLOG_DEBUG("Applied light theme");
    }

    void Theme::ApplyDarkTheme()
    {
        current_scheme_ = GetDarkScheme();
        ApplyImGuiColors();
        SPDLOG_DEBUG("Applied dark theme");
    }

    void Theme::ApplyHighContrastTheme()
    {
        current_scheme_ = GetHighContrastScheme();
        ApplyImGuiColors();
        SPDLOG_DEBUG("Applied high contrast theme");
    }

    void Theme::ApplyColorScheme(const ColorScheme& scheme)
    {
        current_scheme_ = scheme;
        current_theme_ = ThemeType::Custom;
        ApplyImGuiColors();
    }

    void Theme::SetAccentColor(unsigned int color)
    {
        current_scheme_.accent = color;
        
        // Generate hover and active variants
        unsigned int r = (color >> 24) & 0xFF;
        unsigned int g = (color >> 16) & 0xFF;
        unsigned int b = (color >> 8) & 0xFF;
        unsigned int a = color & 0xFF;

        // Lighter for hover
        r = std::min(255u, r + 20);
        g = std::min(255u, g + 20);
        b = std::min(255u, b + 20);
        current_scheme_.accent_hover = (r << 24) | (g << 16) | (b << 8) | a;

        // Darker for active
        r = std::max(0u, (color >> 24) - 20);
        g = std::max(0u, ((color >> 16) & 0xFF) - 20);
        b = std::max(0u, ((color >> 8) & 0xFF) - 20);
        current_scheme_.accent_active = (r << 24) | (g << 16) | (b << 8) | a;

        ApplyImGuiColors();
    }

    void Theme::SetFontConfig(const FontConfig& config)
    {
        font_config_ = config;
        LoadFonts();
    }

    void Theme::SetFontSize(float size)
    {
        font_config_.size = std::clamp(size, 8.0f, 32.0f);
        LoadFonts();
    }

    void Theme::SetIconSize(IconSizePreset preset)
    {
        icon_size_preset_ = preset;
    }

    float Theme::GetIconSize() const
    {
        switch (icon_size_preset_)
        {
        case IconSizePreset::Small: return 16.0f;
        case IconSizePreset::Medium: return 24.0f;
        case IconSizePreset::Large: return 32.0f;
        case IconSizePreset::ExtraLarge: return 48.0f;
        default: return 24.0f;
        }
    }

    ColorScheme Theme::GetLightScheme()
    {
        ColorScheme scheme;
        scheme.name = "Light";
        
        // Main colors (RGBA format: 0xRRGGBBAA)
        scheme.background = 0xFFFFFFFF;
        scheme.background_alt = 0xF5F5F5FF;
        scheme.foreground = 0x1A1A1AFF;
        scheme.foreground_dim = 0x666666FF;
        
        // Accent (Cornflower Blue)
        scheme.accent = 0x6495EDFF;
        scheme.accent_hover = 0x7BA3F0FF;
        scheme.accent_active = 0x4A7BD8FF;
        
        // UI elements
        scheme.border = 0xD0D0D0FF;
        scheme.header = 0xE8E8E8FF;
        scheme.header_hover = 0xDCDCDCFF;
        scheme.header_active = 0xD0D0D0FF;
        
        // Selection
        scheme.selection = 0x6495ED80;
        scheme.selection_inactive = 0xD0D0D080;
        
        // Status
        scheme.error = 0xDC3545FF;
        scheme.warning = 0xFFC107FF;
        scheme.success = 0x28A745FF;
        scheme.info = 0x17A2B8FF;
        
        // File types
        scheme.directory = 0xE6A52CFF;
        scheme.executable = 0x28A745FF;
        scheme.archive = 0x9B59B6FF;
        scheme.image = 0xE74C3CFF;
        scheme.document = 0x3498DBFF;
        scheme.code = 0x2ECC71FF;
        
        return scheme;
    }

    ColorScheme Theme::GetDarkScheme()
    {
        ColorScheme scheme;
        scheme.name = "Dark";
        
        // Main colors
        scheme.background = 0x1E1E1EFF;
        scheme.background_alt = 0x252526FF;
        scheme.foreground = 0xD4D4D4FF;
        scheme.foreground_dim = 0x808080FF;
        
        // Accent (Cornflower Blue)
        scheme.accent = 0x6495EDFF;
        scheme.accent_hover = 0x7BA3F0FF;
        scheme.accent_active = 0x4A7BD8FF;
        
        // UI elements
        scheme.border = 0x3C3C3CFF;
        scheme.header = 0x2D2D30FF;
        scheme.header_hover = 0x3E3E42FF;
        scheme.header_active = 0x4A4A4FFF;
        
        // Selection
        scheme.selection = 0x264F7880;
        scheme.selection_inactive = 0x3A3A3A80;
        
        // Status
        scheme.error = 0xF44747FF;
        scheme.warning = 0xFFCC00FF;
        scheme.success = 0x89D185FF;
        scheme.info = 0x75BEFFFF;
        
        // File types
        scheme.directory = 0xDCDC8BFF;
        scheme.executable = 0x89D185FF;
        scheme.archive = 0xC586C0FF;
        scheme.image = 0xCE9178FF;
        scheme.document = 0x9CDCFEFF;
        scheme.code = 0x4EC9B0FF;
        
        return scheme;
    }

    ColorScheme Theme::GetHighContrastScheme()
    {
        ColorScheme scheme;
        scheme.name = "High Contrast";
        
        // Main colors
        scheme.background = 0x000000FF;
        scheme.background_alt = 0x0A0A0AFF;
        scheme.foreground = 0xFFFFFFFF;
        scheme.foreground_dim = 0xCCCCCCFF;
        
        // Accent (Cyan)
        scheme.accent = 0x00FFFFFF;
        scheme.accent_hover = 0x00E6E6FF;
        scheme.accent_active = 0x00CCCCFF;
        
        // UI elements
        scheme.border = 0xFFFFFFFF;
        scheme.header = 0x1A1A1AFF;
        scheme.header_hover = 0x333333FF;
        scheme.header_active = 0x4D4D4DFF;
        
        // Selection
        scheme.selection = 0x00FFFF80;
        scheme.selection_inactive = 0xFFFFFF40;
        
        // Status
        scheme.error = 0xFF0000FF;
        scheme.warning = 0xFFFF00FF;
        scheme.success = 0x00FF00FF;
        scheme.info = 0x00FFFFFF;
        
        // File types
        scheme.directory = 0xFFFF00FF;
        scheme.executable = 0x00FF00FF;
        scheme.archive = 0xFF00FFFF;
        scheme.image = 0xFF8000FF;
        scheme.document = 0x00FFFFFF;
        scheme.code = 0x00FF80FF;
        
        return scheme;
    }

    void Theme::ApplyImGuiColors()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // Helper to convert our RGBA format to ImVec4
        auto toImVec4 = [](unsigned int color) -> ImVec4 {
            return ImVec4(
                ((color >> 24) & 0xFF) / 255.0f,
                ((color >> 16) & 0xFF) / 255.0f,
                ((color >> 8) & 0xFF) / 255.0f,
                (color & 0xFF) / 255.0f
            );
        };

        // Window
        colors[ImGuiCol_WindowBg] = toImVec4(current_scheme_.background);
        colors[ImGuiCol_ChildBg] = toImVec4(current_scheme_.background);
        colors[ImGuiCol_PopupBg] = toImVec4(current_scheme_.background_alt);

        // Text
        colors[ImGuiCol_Text] = toImVec4(current_scheme_.foreground);
        colors[ImGuiCol_TextDisabled] = toImVec4(current_scheme_.foreground_dim);

        // Borders
        colors[ImGuiCol_Border] = toImVec4(current_scheme_.border);
        colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

        // Frame backgrounds
        colors[ImGuiCol_FrameBg] = toImVec4(current_scheme_.background_alt);
        colors[ImGuiCol_FrameBgHovered] = toImVec4(current_scheme_.header_hover);
        colors[ImGuiCol_FrameBgActive] = toImVec4(current_scheme_.header_active);

        // Title bar
        colors[ImGuiCol_TitleBg] = toImVec4(current_scheme_.header);
        colors[ImGuiCol_TitleBgActive] = toImVec4(current_scheme_.header_active);
        colors[ImGuiCol_TitleBgCollapsed] = toImVec4(current_scheme_.header);

        // Menu bar
        colors[ImGuiCol_MenuBarBg] = toImVec4(current_scheme_.background_alt);

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg] = toImVec4(current_scheme_.background_alt);
        colors[ImGuiCol_ScrollbarGrab] = toImVec4(current_scheme_.header);
        colors[ImGuiCol_ScrollbarGrabHovered] = toImVec4(current_scheme_.header_hover);
        colors[ImGuiCol_ScrollbarGrabActive] = toImVec4(current_scheme_.accent);

        // Buttons
        colors[ImGuiCol_Button] = toImVec4(current_scheme_.accent);
        colors[ImGuiCol_ButtonHovered] = toImVec4(current_scheme_.accent_hover);
        colors[ImGuiCol_ButtonActive] = toImVec4(current_scheme_.accent_active);

        // Headers
        colors[ImGuiCol_Header] = toImVec4(current_scheme_.selection);
        colors[ImGuiCol_HeaderHovered] = toImVec4(current_scheme_.accent_hover);
        colors[ImGuiCol_HeaderActive] = toImVec4(current_scheme_.accent);

        // Separator
        colors[ImGuiCol_Separator] = toImVec4(current_scheme_.border);
        colors[ImGuiCol_SeparatorHovered] = toImVec4(current_scheme_.accent);
        colors[ImGuiCol_SeparatorActive] = toImVec4(current_scheme_.accent_active);

        // Resize grip
        colors[ImGuiCol_ResizeGrip] = toImVec4(current_scheme_.border);
        colors[ImGuiCol_ResizeGripHovered] = toImVec4(current_scheme_.accent);
        colors[ImGuiCol_ResizeGripActive] = toImVec4(current_scheme_.accent_active);

        // Tabs
        colors[ImGuiCol_Tab] = toImVec4(current_scheme_.header);
        colors[ImGuiCol_TabHovered] = toImVec4(current_scheme_.accent_hover);
        colors[ImGuiCol_TabActive] = toImVec4(current_scheme_.accent);
        colors[ImGuiCol_TabUnfocused] = toImVec4(current_scheme_.header);
        colors[ImGuiCol_TabUnfocusedActive] = toImVec4(current_scheme_.header_active);

        // Table
        colors[ImGuiCol_TableHeaderBg] = toImVec4(current_scheme_.header);
        colors[ImGuiCol_TableBorderStrong] = toImVec4(current_scheme_.border);
        colors[ImGuiCol_TableBorderLight] = toImVec4(current_scheme_.border);
        colors[ImGuiCol_TableRowBg] = toImVec4(current_scheme_.background);
        colors[ImGuiCol_TableRowBgAlt] = toImVec4(current_scheme_.background_alt);

        // Navigation highlight
        colors[ImGuiCol_NavHighlight] = toImVec4(current_scheme_.accent);

        // Style adjustments
        style.WindowRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;
        
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;

        style.ItemSpacing = ImVec2(8, 4);
        style.ItemInnerSpacing = ImVec2(4, 4);
        style.WindowPadding = ImVec2(8, 8);
        style.FramePadding = ImVec2(6, 4);
    }

    void Theme::LoadFonts()
    {
        // Font loading would be done here
        // For now, use default ImGui font
        SPDLOG_DEBUG("Font configuration updated: {} {}pt", font_config_.family, font_config_.size);
    }

    unsigned int Theme::GetFileColor(const std::string& extension) const
    {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Executables
        if (ext == ".exe" || ext == ".msi" || ext == ".bat" || ext == ".cmd" || ext == ".ps1")
            return current_scheme_.executable;

        // Archives
        if (ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".tar" || ext == ".gz")
            return current_scheme_.archive;

        // Images
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp" || 
            ext == ".ico" || ext == ".svg" || ext == ".webp")
            return current_scheme_.image;

        // Documents
        if (ext == ".pdf" || ext == ".doc" || ext == ".docx" || ext == ".xls" || ext == ".xlsx" ||
            ext == ".ppt" || ext == ".pptx" || ext == ".txt" || ext == ".rtf")
            return current_scheme_.document;

        // Code
        if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".cs" ||
            ext == ".py" || ext == ".js" || ext == ".ts" || ext == ".java" || ext == ".rs" ||
            ext == ".go" || ext == ".rb" || ext == ".php" || ext == ".html" || ext == ".css")
            return current_scheme_.code;

        // Default
        return current_scheme_.foreground;
    }

    bool Theme::LoadTheme(const std::string& path)
    {
        // TODO: Implement JSON loading
        SPDLOG_INFO("Loading theme from: {}", path);
        return false;
    }

    bool Theme::SaveTheme(const std::string& path) const
    {
        // TODO: Implement JSON saving
        SPDLOG_INFO("Saving theme to: {}", path);
        return false;
    }

    bool Theme::ExportTheme(const std::string& path) const
    {
        return SaveTheme(path);
    }

    bool Theme::ImportTheme(const std::string& path)
    {
        return LoadTheme(path);
    }

} // namespace opacity::ui
