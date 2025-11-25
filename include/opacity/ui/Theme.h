#pragma once

namespace opacity::ui
{
    /**
     * @brief Theme management system
     */
    class Theme
    {
    public:
        Theme() = default;
        ~Theme() = default;

        void ApplyLightTheme();
        void ApplyDarkTheme();
    };

} // namespace opacity::ui
