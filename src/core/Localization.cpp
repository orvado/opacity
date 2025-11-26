// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Opacity Project

#include "opacity/core/Localization.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <ctime>
#include <regex>
#include <set>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace opacity { namespace core {

FormatArg::FormatArg(const std::string& n, double v) : name(n) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    value = oss.str();
}

class Localization::Impl {
public:
    std::string localesDir;
    std::string currentLanguage = "en";
    std::string fallbackLanguage = "en";
    
    std::vector<Language> availableLanguages;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> strings;
    std::vector<LanguageChangedCallback> callbacks;
    
    bool showMissingKeys = false;
    mutable std::set<std::string> usedKeys;
    bool initialized = false;
    
    bool loadLanguage(const std::string& code) {
        std::string filePath = localesDir + "/" + code + ".json";
        if (!fs::exists(filePath)) {
            spdlog::warn("Localization: language file not found: {}", filePath);
            return false;
        }
        
        try {
            std::ifstream file(filePath);
            if (!file) {
                return false;
            }
            
            json j = json::parse(file);
            
            std::unordered_map<std::string, std::string> langStrings;
            flattenJson(j, "", langStrings);
            
            strings[code] = std::move(langStrings);
            spdlog::info("Localization: loaded {} strings for '{}'", 
                        strings[code].size(), code);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("Localization: failed to load {}: {}", code, e.what());
            return false;
        }
    }
    
    void flattenJson(const json& j, const std::string& prefix, 
                     std::unordered_map<std::string, std::string>& result) {
        for (auto& [key, value] : j.items()) {
            std::string fullKey = prefix.empty() ? key : prefix + "." + key;
            
            if (value.is_string()) {
                result[fullKey] = value.get<std::string>();
            } else if (value.is_object()) {
                flattenJson(value, fullKey, result);
            }
        }
    }
    
    void scanLanguages() {
        availableLanguages.clear();
        
        if (!fs::exists(localesDir)) {
            spdlog::warn("Localization: locales directory not found: {}", localesDir);
            return;
        }
        
        for (const auto& entry : fs::directory_iterator(localesDir)) {
            if (entry.path().extension() == ".json") {
                std::string code = entry.path().stem().string();
                
                // Try to load language info
                Language lang;
                lang.code = code;
                
                try {
                    std::ifstream file(entry.path());
                    json j = json::parse(file);
                    
                    if (j.contains("_meta")) {
                        auto& meta = j["_meta"];
                        if (meta.contains("name")) lang.name = meta["name"];
                        if (meta.contains("englishName")) lang.englishName = meta["englishName"];
                        if (meta.contains("region")) lang.region = meta["region"];
                        if (meta.contains("rtl")) lang.isRTL = meta["rtl"];
                        if (meta.contains("fontFamily")) lang.fontFamily = meta["fontFamily"];
                        if (meta.contains("fontSize")) lang.fontSize = meta["fontSize"];
                    }
                } catch (...) {
                    // Use defaults
                }
                
                if (lang.name.empty()) {
                    lang.name = getLanguageName(code);
                }
                if (lang.englishName.empty()) {
                    lang.englishName = lang.name;
                }
                
                availableLanguages.push_back(lang);
            }
        }
        
        // Sort by name
        std::sort(availableLanguages.begin(), availableLanguages.end(),
            [](const Language& a, const Language& b) {
                return a.englishName < b.englishName;
            });
    }
    
    static std::string getLanguageName(const std::string& code) {
        static std::unordered_map<std::string, std::string> names = {
            {"en", "English"},
            {"de", "Deutsch"},
            {"fr", "Français"},
            {"es", "Español"},
            {"it", "Italiano"},
            {"ja", "日本語"},
            {"ko", "한국어"},
            {"zh", "中文"},
            {"pt", "Português"},
            {"ru", "Русский"},
            {"ar", "العربية"},
            {"hi", "हिन्दी"},
            {"tr", "Türkçe"},
            {"pl", "Polski"},
            {"nl", "Nederlands"},
            {"sv", "Svenska"},
            {"no", "Norsk"},
            {"da", "Dansk"},
            {"fi", "Suomi"},
            {"cs", "Čeština"},
            {"uk", "Українська"},
            {"he", "עברית"},
            {"th", "ไทย"},
            {"vi", "Tiếng Việt"},
            {"id", "Bahasa Indonesia"}
        };
        
        auto it = names.find(code);
        return it != names.end() ? it->second : code;
    }
    
    std::string getString(const std::string& key, const std::string& defaultValue = "") const {
        usedKeys.insert(key);
        
        // Try current language
        auto langIt = strings.find(currentLanguage);
        if (langIt != strings.end()) {
            auto strIt = langIt->second.find(key);
            if (strIt != langIt->second.end()) {
                return strIt->second;
            }
        }
        
        // Try fallback language
        if (currentLanguage != fallbackLanguage) {
            langIt = strings.find(fallbackLanguage);
            if (langIt != strings.end()) {
                auto strIt = langIt->second.find(key);
                if (strIt != langIt->second.end()) {
                    return strIt->second;
                }
            }
        }
        
        // Return default or key
        if (showMissingKeys) {
            spdlog::warn("Localization: missing key '{}'", key);
            return "[" + key + "]";
        }
        
        return defaultValue.empty() ? key : defaultValue;
    }
    
    std::string formatString(const std::string& text, 
                            const std::vector<FormatArg>& args) const {
        std::string result = text;
        
        for (const auto& arg : args) {
            std::string placeholder = "{" + arg.name + "}";
            size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                result.replace(pos, placeholder.length(), arg.value);
                pos += arg.value.length();
            }
        }
        
        return result;
    }
    
    PluralRule getPluralRule(const std::string& lang, int n) const {
        // Simplified plural rules for common languages
        // See: https://cldr.unicode.org/index/cldr-spec/plural-rules
        
        if (n == 0) return PluralRule::Zero;
        if (n == 1) return PluralRule::One;
        if (n == 2) return PluralRule::Two;
        
        // East Asian languages: no plural forms
        if (lang == "ja" || lang == "ko" || lang == "zh" || lang == "vi") {
            return PluralRule::Other;
        }
        
        // Slavic languages
        if (lang == "ru" || lang == "uk" || lang == "pl" || lang == "cs") {
            int mod10 = n % 10;
            int mod100 = n % 100;
            
            if (mod10 == 1 && mod100 != 11) return PluralRule::One;
            if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return PluralRule::Few;
            return PluralRule::Many;
        }
        
        // Arabic
        if (lang == "ar") {
            if (n >= 3 && n <= 10) return PluralRule::Few;
            if (n >= 11) return PluralRule::Many;
        }
        
        // Most Western languages
        return PluralRule::Other;
    }
    
    std::string getPluralKey(const std::string& baseKey, PluralRule rule) const {
        static std::unordered_map<PluralRule, std::string> suffixes = {
            {PluralRule::Zero, "_zero"},
            {PluralRule::One, "_one"},
            {PluralRule::Two, "_two"},
            {PluralRule::Few, "_few"},
            {PluralRule::Many, "_many"},
            {PluralRule::Other, "_other"}
        };
        
        return baseKey + suffixes[rule];
    }
};

Localization::Localization() : pImpl(std::make_unique<Impl>()) {}

Localization::~Localization() {
    shutdown();
}

bool Localization::initialize(const std::string& localesDirectory) {
    if (pImpl->initialized) {
        return true;
    }
    
    pImpl->localesDir = localesDirectory;
    
    // Create default locale if needed
    if (!fs::exists(localesDirectory)) {
        fs::create_directories(localesDirectory);
        
        // Create default English strings
        json en;
        en["_meta"]["name"] = "English";
        en["_meta"]["englishName"] = "English";
        en["_meta"]["region"] = "US";
        
        en["ui"]["ok"] = "OK";
        en["ui"]["cancel"] = "Cancel";
        en["ui"]["yes"] = "Yes";
        en["ui"]["no"] = "No";
        en["ui"]["apply"] = "Apply";
        en["ui"]["close"] = "Close";
        en["ui"]["save"] = "Save";
        en["ui"]["open"] = "Open";
        en["ui"]["file"] = "File";
        en["ui"]["edit"] = "Edit";
        en["ui"]["view"] = "View";
        en["ui"]["help"] = "Help";
        en["ui"]["error"] = "Error";
        en["ui"]["warning"] = "Warning";
        en["ui"]["info"] = "Information";
        
        en["app"]["title"] = "Opacity File Explorer";
        en["app"]["version"] = "Version {version}";
        
        en["file"]["name"] = "Name";
        en["file"]["size"] = "Size";
        en["file"]["date_modified"] = "Date Modified";
        en["file"]["type"] = "Type";
        en["file"]["items_one"] = "{count} item";
        en["file"]["items_other"] = "{count} items";
        en["file"]["selected_one"] = "{count} item selected";
        en["file"]["selected_other"] = "{count} items selected";
        
        en["size"]["bytes"] = "bytes";
        en["size"]["kb"] = "KB";
        en["size"]["mb"] = "MB";
        en["size"]["gb"] = "GB";
        en["size"]["tb"] = "TB";
        
        en["time"]["now"] = "just now";
        en["time"]["minute_one"] = "{count} minute ago";
        en["time"]["minute_other"] = "{count} minutes ago";
        en["time"]["hour_one"] = "{count} hour ago";
        en["time"]["hour_other"] = "{count} hours ago";
        en["time"]["day_one"] = "{count} day ago";
        en["time"]["day_other"] = "{count} days ago";
        
        std::ofstream file(localesDirectory + "/en.json");
        if (file) {
            file << en.dump(2);
        }
    }
    
    pImpl->scanLanguages();
    pImpl->loadLanguage("en");
    
    // Try to use system language
    std::string systemLang = detectSystemLanguage();
    if (!systemLang.empty() && systemLang != "en") {
        if (pImpl->loadLanguage(systemLang)) {
            pImpl->currentLanguage = systemLang;
        }
    }
    
    pImpl->initialized = true;
    spdlog::info("Localization: initialized with language '{}'", pImpl->currentLanguage);
    return true;
}

void Localization::shutdown() {
    if (pImpl->initialized) {
        pImpl->initialized = false;
    }
}

std::vector<Language> Localization::getAvailableLanguages() const {
    return pImpl->availableLanguages;
}

bool Localization::setLanguage(const std::string& languageCode) {
    if (languageCode == pImpl->currentLanguage) {
        return true;
    }
    
    // Load if not already loaded
    if (pImpl->strings.find(languageCode) == pImpl->strings.end()) {
        if (!pImpl->loadLanguage(languageCode)) {
            return false;
        }
    }
    
    pImpl->currentLanguage = languageCode;
    
    // Notify listeners
    for (const auto& callback : pImpl->callbacks) {
        try {
            callback(languageCode);
        } catch (...) {}
    }
    
    spdlog::info("Localization: switched to '{}'", languageCode);
    return true;
}

std::string Localization::getCurrentLanguage() const {
    return pImpl->currentLanguage;
}

const Language* Localization::getCurrentLanguageInfo() const {
    for (const auto& lang : pImpl->availableLanguages) {
        if (lang.code == pImpl->currentLanguage) {
            return &lang;
        }
    }
    return nullptr;
}

bool Localization::isRTL() const {
    auto* lang = getCurrentLanguageInfo();
    return lang ? lang->isRTL : false;
}

std::string Localization::detectSystemLanguage() const {
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
        // Convert wide string to narrow string
        int len = WideCharToMultiByte(CP_UTF8, 0, localeName, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string locale(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, localeName, -1, &locale[0], len, nullptr, nullptr);
            
            // Extract language code (before the hyphen)
            size_t hyphen = locale.find('-');
            if (hyphen != std::string::npos) {
                return locale.substr(0, hyphen);
            }
            return locale;
        }
    }
    
    return "en";
}

bool Localization::useSystemLanguage() {
    std::string lang = detectSystemLanguage();
    return setLanguage(lang);
}

std::string Localization::get(const std::string& key) const {
    return pImpl->getString(key);
}

std::string Localization::get(const std::string& key, const std::string& defaultValue) const {
    return pImpl->getString(key, defaultValue);
}

std::string Localization::getContext(const std::string& context, const std::string& key) const {
    // Try context-specific key first
    std::string contextKey = context + "." + key;
    std::string result = pImpl->getString(contextKey, "");
    if (!result.empty() && result != contextKey) {
        return result;
    }
    // Fall back to non-context key
    return pImpl->getString(key);
}

std::string Localization::format(const std::string& key, 
                                 const std::vector<FormatArg>& args) const {
    std::string text = get(key);
    return pImpl->formatString(text, args);
}

std::string Localization::format(const std::string& key,
                                 std::initializer_list<FormatArg> args) const {
    return format(key, std::vector<FormatArg>(args));
}

std::string Localization::plural(const std::string& key, int count) const {
    return plural(key, count, {FormatArg("count", count)});
}

std::string Localization::plural(const std::string& key, int count,
                                 const std::vector<FormatArg>& args) const {
    PluralRule rule = pImpl->getPluralRule(pImpl->currentLanguage, count);
    std::string pluralKey = pImpl->getPluralKey(key, rule);
    
    // Try specific plural form
    std::string text = pImpl->getString(pluralKey, "");
    if (text.empty() || text == pluralKey) {
        // Try _other form
        pluralKey = pImpl->getPluralKey(key, PluralRule::Other);
        text = pImpl->getString(pluralKey, "");
        if (text.empty() || text == pluralKey) {
            // Fall back to base key
            text = get(key);
        }
    }
    
    return pImpl->formatString(text, args);
}

std::string Localization::formatNumber(double number, int decimals) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << number;
    std::string result = oss.str();
    
    // Add thousand separators (simplified)
    // In production, use locale-specific formatting
    return result;
}

std::string Localization::formatInteger(int64_t number) const {
    std::string result = std::to_string(number);
    
    // Add thousand separators
    int insertPosition = static_cast<int>(result.length()) - 3;
    while (insertPosition > 0) {
        result.insert(insertPosition, ",");
        insertPosition -= 3;
    }
    
    return result;
}

std::string Localization::formatFileSize(uint64_t bytes) const {
    static const char* units[] = {"bytes", "KB", "MB", "GB", "TB"};
    
    if (bytes == 0) {
        return "0 " + get("size.bytes", "bytes");
    }
    
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024 && unitIndex < 4) {
        size /= 1024;
        unitIndex++;
    }
    
    std::string unitKey = std::string("size.") + 
        (unitIndex == 0 ? "bytes" : 
         unitIndex == 1 ? "kb" : 
         unitIndex == 2 ? "mb" : 
         unitIndex == 3 ? "gb" : "tb");
    
    std::ostringstream oss;
    if (unitIndex == 0) {
        oss << bytes;
    } else {
        oss << std::fixed << std::setprecision(size < 10 ? 2 : (size < 100 ? 1 : 0)) << size;
    }
    
    return oss.str() + " " + get(unitKey, units[unitIndex]);
}

std::string Localization::formatDate(time_t timestamp, const std::string& fmt) const {
    std::tm tm;
    localtime_s(&tm, &timestamp);
    
    std::ostringstream oss;
    if (fmt.empty()) {
        oss << std::put_time(&tm, "%x"); // Locale date format
    } else {
        oss << std::put_time(&tm, fmt.c_str());
    }
    
    return oss.str();
}

std::string Localization::formatDateTime(time_t timestamp, const std::string& fmt) const {
    std::tm tm;
    localtime_s(&tm, &timestamp);
    
    std::ostringstream oss;
    if (fmt.empty()) {
        oss << std::put_time(&tm, "%c"); // Locale date and time
    } else {
        oss << std::put_time(&tm, fmt.c_str());
    }
    
    return oss.str();
}

std::string Localization::formatRelativeTime(time_t timestamp) const {
    time_t now = std::time(nullptr);
    int64_t diff = now - timestamp;
    
    if (diff < 0) {
        // Future time, just return date
        return formatDateTime(timestamp);
    }
    
    if (diff < 60) {
        return get("time.now", "just now");
    }
    
    if (diff < 3600) {
        int minutes = static_cast<int>(diff / 60);
        return plural("time.minute", minutes);
    }
    
    if (diff < 86400) {
        int hours = static_cast<int>(diff / 3600);
        return plural("time.hour", hours);
    }
    
    if (diff < 604800) { // 7 days
        int days = static_cast<int>(diff / 86400);
        return plural("time.day", days);
    }
    
    // Older than a week, show date
    return formatDate(timestamp);
}

bool Localization::addString(const std::string& key, const std::string& value) {
    pImpl->strings[pImpl->currentLanguage][key] = value;
    return true;
}

bool Localization::hasKey(const std::string& key) const {
    auto langIt = pImpl->strings.find(pImpl->currentLanguage);
    if (langIt != pImpl->strings.end()) {
        return langIt->second.find(key) != langIt->second.end();
    }
    
    langIt = pImpl->strings.find(pImpl->fallbackLanguage);
    if (langIt != pImpl->strings.end()) {
        return langIt->second.find(key) != langIt->second.end();
    }
    
    return false;
}

std::vector<std::string> Localization::getMissingKeys(const std::string& languageCode) const {
    std::vector<std::string> missing;
    
    auto enIt = pImpl->strings.find("en");
    if (enIt == pImpl->strings.end()) {
        return missing;
    }
    
    auto langIt = pImpl->strings.find(languageCode);
    if (langIt == pImpl->strings.end()) {
        // All keys are missing
        for (const auto& [key, value] : enIt->second) {
            missing.push_back(key);
        }
        return missing;
    }
    
    for (const auto& [key, value] : enIt->second) {
        if (langIt->second.find(key) == langIt->second.end()) {
            missing.push_back(key);
        }
    }
    
    return missing;
}

void Localization::addLanguageChangedCallback(LanguageChangedCallback callback) {
    pImpl->callbacks.push_back(std::move(callback));
}

bool Localization::exportStrings(const std::string& filePath, const std::string& languageCode) const {
    std::string lang = languageCode.empty() ? pImpl->currentLanguage : languageCode;
    
    auto it = pImpl->strings.find(lang);
    if (it == pImpl->strings.end()) {
        return false;
    }
    
    try {
        // Convert flat strings back to nested JSON
        json j;
        for (const auto& [key, value] : it->second) {
            // Split key by dots
            std::vector<std::string> parts;
            std::istringstream iss(key);
            std::string part;
            while (std::getline(iss, part, '.')) {
                parts.push_back(part);
            }
            
            // Build nested structure
            json* current = &j;
            for (size_t i = 0; i < parts.size() - 1; ++i) {
                if (!current->contains(parts[i])) {
                    (*current)[parts[i]] = json::object();
                }
                current = &(*current)[parts[i]];
            }
            (*current)[parts.back()] = value;
        }
        
        std::ofstream file(filePath);
        if (!file) {
            return false;
        }
        
        file << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool Localization::importStrings(const std::string& filePath) {
    try {
        std::ifstream file(filePath);
        if (!file) {
            return false;
        }
        
        json j = json::parse(file);
        
        std::unordered_map<std::string, std::string> strings;
        pImpl->flattenJson(j, "", strings);
        
        for (const auto& [key, value] : strings) {
            pImpl->strings[pImpl->currentLanguage][key] = value;
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void Localization::setShowMissingKeys(bool show) {
    pImpl->showMissingKeys = show;
}

std::vector<std::string> Localization::getUsedKeys() const {
    return std::vector<std::string>(pImpl->usedKeys.begin(), pImpl->usedKeys.end());
}

}} // namespace opacity::core
