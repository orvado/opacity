// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#ifndef OPACITY_CORE_LOCALIZATION_H
#define OPACITY_CORE_LOCALIZATION_H

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>

namespace opacity { namespace core {

/**
 * @brief Language information
 */
struct Language {
    std::string code;           // ISO 639-1 code (e.g., "en", "de", "ja")
    std::string name;           // Native name (e.g., "English", "Deutsch")
    std::string englishName;    // English name
    std::string region;         // ISO 3166-1 alpha-2 (e.g., "US", "DE")
    bool isRTL = false;         // Right-to-left
    std::string fontFamily;     // Preferred font family
    float fontSize = 1.0f;      // Font size multiplier
};

/**
 * @brief Plural form rules
 */
enum class PluralRule {
    Zero,       // n == 0
    One,        // n == 1 (or language-specific)
    Two,        // n == 2
    Few,        // 2-4, language-specific
    Many,       // 5+, language-specific
    Other       // Default/other
};

/**
 * @brief String format argument
 */
struct FormatArg {
    std::string name;
    std::string value;
    
    FormatArg(const std::string& n, const std::string& v) : name(n), value(v) {}
    FormatArg(const std::string& n, int v) : name(n), value(std::to_string(v)) {}
    FormatArg(const std::string& n, double v);
};

/**
 * @brief Provides internationalization (i18n) support
 * 
 * Features:
 * - Load string tables from JSON files
 * - Support for pluralization
 * - String formatting with named parameters
 * - Fallback to default language
 * - RTL language support
 * - Dynamic language switching
 */
class Localization {
public:
    using LanguageChangedCallback = std::function<void(const std::string&)>;
    
    Localization();
    ~Localization();
    
    // Initialization
    bool initialize(const std::string& localesDirectory);
    void shutdown();
    
    // Language management
    std::vector<Language> getAvailableLanguages() const;
    bool setLanguage(const std::string& languageCode);
    std::string getCurrentLanguage() const;
    const Language* getCurrentLanguageInfo() const;
    bool isRTL() const;
    
    // Auto-detection
    std::string detectSystemLanguage() const;
    bool useSystemLanguage();
    
    // Basic translation
    std::string get(const std::string& key) const;
    std::string get(const std::string& key, const std::string& defaultValue) const;
    
    // Translation with context
    std::string getContext(const std::string& context, const std::string& key) const;
    
    // Formatted translation
    std::string format(const std::string& key, 
                      const std::vector<FormatArg>& args) const;
    std::string format(const std::string& key,
                      std::initializer_list<FormatArg> args) const;
    
    // Plural forms
    std::string plural(const std::string& key, int count) const;
    std::string plural(const std::string& key, int count, 
                      const std::vector<FormatArg>& args) const;
    
    // Number and date formatting
    std::string formatNumber(double number, int decimals = 2) const;
    std::string formatInteger(int64_t number) const;
    std::string formatFileSize(uint64_t bytes) const;
    std::string formatDate(time_t timestamp, const std::string& format = "") const;
    std::string formatDateTime(time_t timestamp, const std::string& format = "") const;
    std::string formatRelativeTime(time_t timestamp) const;
    
    // Shortcuts for common UI strings
    std::string ok() const { return get("ui.ok", "OK"); }
    std::string cancel() const { return get("ui.cancel", "Cancel"); }
    std::string yes() const { return get("ui.yes", "Yes"); }
    std::string no() const { return get("ui.no", "No"); }
    std::string apply() const { return get("ui.apply", "Apply"); }
    std::string close() const { return get("ui.close", "Close"); }
    std::string save() const { return get("ui.save", "Save"); }
    std::string open() const { return get("ui.open", "Open"); }
    std::string fileStr() const { return get("ui.file", "File"); }
    std::string edit() const { return get("ui.edit", "Edit"); }
    std::string view() const { return get("ui.view", "View"); }
    std::string help() const { return get("ui.help", "Help"); }
    std::string error() const { return get("ui.error", "Error"); }
    std::string warning() const { return get("ui.warning", "Warning"); }
    std::string info() const { return get("ui.info", "Information"); }
    
    // String table operations
    bool addString(const std::string& key, const std::string& value);
    bool hasKey(const std::string& key) const;
    std::vector<std::string> getMissingKeys(const std::string& languageCode) const;
    
    // Event handling
    void addLanguageChangedCallback(LanguageChangedCallback callback);
    
    // Import/Export
    bool exportStrings(const std::string& filePath, const std::string& languageCode = "") const;
    bool importStrings(const std::string& filePath);
    
    // Debug
    void setShowMissingKeys(bool show);
    std::vector<std::string> getUsedKeys() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Convenience macros for translation
#define _(key) opacity::core::Localization::instance().get(key)
#define _F(key, ...) opacity::core::Localization::instance().format(key, {__VA_ARGS__})
#define _P(key, count) opacity::core::Localization::instance().plural(key, count)

}} // namespace opacity::core

#endif // OPACITY_CORE_LOCALIZATION_H
