#include "opacity/core/Path.h"

namespace opacity::core
{
    Path::Path(const std::filesystem::path& path)
        : path_(path)
    {
    }

    Path::Path(const std::string& path_str)
        : path_(path_str)
    {
    }

    Path::Path(const wchar_t* path_cstr)
        : path_(path_cstr)
    {
    }

    Path Path::Parent() const
    {
        return Path(path_.parent_path());
    }

    std::string Path::Filename() const
    {
        return path_.filename().string();
    }

    std::string Path::Extension() const
    {
        return path_.extension().string();
    }

    std::string Path::StemName() const
    {
        return path_.stem().string();
    }

    bool Path::Exists() const
    {
        return std::filesystem::exists(path_);
    }

    bool Path::IsDirectory() const
    {
        return std::filesystem::is_directory(path_);
    }

    bool Path::IsFile() const
    {
        return std::filesystem::is_regular_file(path_);
    }

    bool Path::IsSymlink() const
    {
        return std::filesystem::is_symlink(path_);
    }

    bool Path::IsRelative() const
    {
        return path_.is_relative();
    }

    bool Path::IsAbsolute() const
    {
        return path_.is_absolute();
    }

    Path Path::operator/(const std::string& component) const
    {
        return Path(path_ / component);
    }

    Path Path::operator/(const Path& other) const
    {
        return Path(path_ / other.path_);
    }

    bool Path::operator==(const Path& other) const
    {
        return path_ == other.path_;
    }

    bool Path::operator!=(const Path& other) const
    {
        return path_ != other.path_;
    }

    bool Path::IsNetworkPath() const
    {
        std::string path_str = path_.string();
        return path_str.length() >= 2 && path_str[0] == '\\' && path_str[1] == '\\';
    }

    bool Path::IsJunction() const
    {
        // TODO: Implement Win32 API call to check for junction
        return false;
    }

    Path Path::GetJunctionTarget() const
    {
        // TODO: Implement Win32 API call to get junction target
        return Path();
    }

} // namespace opacity::core
