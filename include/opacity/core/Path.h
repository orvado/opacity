#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace opacity::core
{
    /**
     * @brief Path abstraction wrapper for Windows filesystem operations
     * 
     * Provides unified interface for path operations, combining
     * std::filesystem::path with Win32 API where needed.
     */
    class Path
    {
    public:
        explicit Path(const std::filesystem::path& path = std::filesystem::path());
        explicit Path(const std::string& path_str);
        explicit Path(const wchar_t* path_cstr);

        // Core path operations
        [[nodiscard]] std::filesystem::path Get() const { return path_; }
        [[nodiscard]] std::string String() const { return path_.string(); }
        [[nodiscard]] std::wstring WString() const { return path_.wstring(); }

        // Path components
        [[nodiscard]] Path Parent() const;
        [[nodiscard]] std::string Filename() const;
        [[nodiscard]] std::string Extension() const;
        [[nodiscard]] std::string StemName() const;

        // Path checks
        [[nodiscard]] bool Exists() const;
        [[nodiscard]] bool IsDirectory() const;
        [[nodiscard]] bool IsFile() const;
        [[nodiscard]] bool IsSymlink() const;
        [[nodiscard]] bool IsRelative() const;
        [[nodiscard]] bool IsAbsolute() const;

        // Path composition
        Path operator/(const std::string& component) const;
        Path operator/(const Path& other) const;

        // Operators
        bool operator==(const Path& other) const;
        bool operator!=(const Path& other) const;

        // Win32 specific operations
        [[nodiscard]] bool IsNetworkPath() const;
        [[nodiscard]] bool IsJunction() const;
        [[nodiscard]] Path GetJunctionTarget() const;

    private:
        std::filesystem::path path_;
    };

} // namespace opacity::core
