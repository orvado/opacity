#pragma once

#include <string>
#include <array>
#include <functional>

namespace opacity::ui
{
    /**
     * @brief Color scheme definition
     */
    struct ColorScheme
    {
        std::string name;
        
        // Main colors (RGBA as 0xRRGGBBAA)
        unsigned int background;
        unsigned int background_alt;
        unsigned int foreground;
        unsigned int foreground_dim;
        
        // Accent colors
        unsigned int accent;
        unsigned int accent_hover;
        unsigned int accent_active;
        
        // UI element colors
        unsigned int border;
        unsigned int header;
        unsigned int header_hover;
        unsigned int header_active;
        
        // Selection colors
        unsigned int selection;
        unsigned int selection_inactive;
        
        // Status colors
        unsigned int error;
        unsigned int warning;
        unsigned int success;
        unsigned int info;
        
        // File type colors
        unsigned int directory;
        unsigned int executable;
        unsigned int archive;
        unsigned int image;
        unsigned int document;
        unsigned int code;
    };

    /**
     * @brief Font configuration
     */
    struct FontConfig
    {
        std::string family = "Segoe UI";
        float size = 14.0f;
        bool bold = false;
        bool italic = false;
        
        // Icon font settings
        std::string icon_font = "";
        float icon_size = 16.0f;
    };

    /**
     * @brief Theme type enumeration
     */
    enum class ThemeType
    {
        Light,
        Dark,
        HighContrast,
        Custom
    };

    /**
     * @brief Theme management system with customization support
     * 
     * Features:
     * - Light/Dark/High-contrast themes
     * - Custom accent colors
     * - Font configuration
     * - Theme import/export
     */
    class Theme
    {
    public:
        using ThemeChangedCallback = std::function<void(ThemeType)>;

        Theme();
        ~Theme() = default;

        /**
         * @brief Initialize the theme system
         */
        void Initialize();

        /**
         * @brief Apply a predefined theme
         */
        void ApplyTheme(ThemeType type);
        
        /**
         * @brief Apply the light theme
         */
        void ApplyLightTheme();
        
        /**
         * @brief Apply the dark theme
         */
        void ApplyDarkTheme();
        
        /**
         * @brief Apply high contrast theme
         */
        void ApplyHighContrastTheme();

        /**
         * @brief Apply a custom color scheme
         */
        void ApplyColorScheme(const ColorScheme& scheme);

        /**
         * @brief Get the current theme type
         */
        ThemeType GetCurrentTheme() const { return current_theme_; }

        /**
         * @brief Get the current color scheme
         */
        const ColorScheme& GetColorScheme() const { return current_scheme_; }

        // Accent color customization
        void SetAccentColor(unsigned int color);
        unsigned int GetAccentColor() const { return current_scheme_.accent; }

        // Font configuration
        void SetFontConfig(const FontConfig& config);
        const FontConfig& GetFontConfig() const { return font_config_; }
        void SetFontSize(float size);
        float GetFontSize() const { return font_config_.size; }

        // Icon size presets
        enum class IconSizePreset { Small, Medium, Large, ExtraLarge };
        void SetIconSize(IconSizePreset preset);
        float GetIconSize() const;

        // Theme persistence
        bool LoadTheme(const std::string& path);
        bool SaveTheme(const std::string& path) const;
        bool ExportTheme(const std::string& path) const;
        bool ImportTheme(const std::string& path);

        // Predefined color schemes
        static ColorScheme GetLightScheme();
        static ColorScheme GetDarkScheme();
        static ColorScheme GetHighContrastScheme();

        // Callbacks
        void SetThemeChangedCallback(ThemeChangedCallback callback) { on_theme_changed_ = std::move(callback); }

        // UI colors for specific elements
        unsigned int GetDirectoryColor() const { return current_scheme_.directory; }
        unsigned int GetExecutableColor() const { return current_scheme_.executable; }
        unsigned int GetArchiveColor() const { return current_scheme_.archive; }
        unsigned int GetImageColor() const { return current_scheme_.image; }
        unsigned int GetDocumentColor() const { return current_scheme_.document; }
        unsigned int GetCodeColor() const { return current_scheme_.code; }

        /**
         * @brief Get file color based on extension
         */
        unsigned int GetFileColor(const std::string& extension) const;

    private:
        void ApplyImGuiColors();
        void LoadFonts();

        ThemeType current_theme_ = ThemeType::Dark;
        ColorScheme current_scheme_;
        FontConfig font_config_;
        IconSizePreset icon_size_preset_ = IconSizePreset::Medium;

        ThemeChangedCallback on_theme_changed_;
    };

} // namespace opacity::ui
