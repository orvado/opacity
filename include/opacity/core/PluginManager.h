#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace opacity::core
{
    // Forward declarations
    class PluginManager;

    /**
     * @brief Plugin API version for compatibility checking
     */
    constexpr int PLUGIN_API_VERSION_MAJOR = 1;
    constexpr int PLUGIN_API_VERSION_MINOR = 0;

    /**
     * @brief Plugin capability flags
     */
    enum class PluginCapability : uint32_t
    {
        None            = 0,
        PreviewHandler  = 1 << 0,   // Can preview specific file types
        FileOperation   = 1 << 1,   // Provides custom file operations
        SearchProvider  = 1 << 2,   // Custom search functionality
        UIExtension     = 1 << 3,   // Extends UI (toolbars, menus)
        ContextMenu     = 1 << 4,   // Adds context menu items
        ColumnProvider  = 1 << 5,   // Provides custom columns
        ThemeProvider   = 1 << 6,   // Provides custom themes
        CommandProvider = 1 << 7,   // Provides command palette commands
    };

    inline PluginCapability operator|(PluginCapability a, PluginCapability b) {
        return static_cast<PluginCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline PluginCapability operator&(PluginCapability a, PluginCapability b) {
        return static_cast<PluginCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline bool HasCapability(PluginCapability caps, PluginCapability flag) {
        return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(flag)) != 0;
    }

    /**
     * @brief Plugin state enumeration
     */
    enum class PluginState
    {
        Unloaded,       // Not loaded
        Loading,        // Currently loading
        Loaded,         // Loaded but not initialized
        Initializing,   // Being initialized
        Active,         // Fully active
        Disabling,      // Being disabled
        Disabled,       // Loaded but disabled
        Error           // Error state
    };

    /**
     * @brief Plugin metadata structure
     */
    struct PluginInfo
    {
        std::string id;                     // Unique identifier (e.g., "com.example.myplugin")
        std::string name;                   // Display name
        std::string description;            // Plugin description
        std::string author;                 // Author name
        std::string version;                // Version string (e.g., "1.0.0")
        std::string website;                // Optional website URL
        
        int apiVersionMajor = 0;            // Required API version
        int apiVersionMinor = 0;
        
        PluginCapability capabilities = PluginCapability::None;
        std::vector<std::string> supportedExtensions;  // For preview handlers
        std::vector<std::string> dependencies;         // Plugin IDs this depends on
        
        std::filesystem::path dllPath;      // Path to the DLL
        PluginState state = PluginState::Unloaded;
        std::string errorMessage;           // If in error state
        
        bool IsCompatible() const {
            return apiVersionMajor == PLUGIN_API_VERSION_MAJOR &&
                   apiVersionMinor <= PLUGIN_API_VERSION_MINOR;
        }
    };

    /**
     * @brief Plugin settings for persistence
     */
    struct PluginSettings
    {
        std::string pluginId;
        bool enabled = true;
        std::unordered_map<std::string, std::string> settings;
    };

    /**
     * @brief Base interface for all plugins
     * 
     * Plugins must implement this interface and export the required functions.
     */
    class IPlugin
    {
    public:
        virtual ~IPlugin() = default;

        /**
         * @brief Get plugin information
         */
        virtual const PluginInfo& GetInfo() const = 0;

        /**
         * @brief Initialize the plugin
         * @param manager Reference to the plugin manager for callbacks
         * @return true if initialization succeeded
         */
        virtual bool Initialize(PluginManager* manager) = 0;

        /**
         * @brief Shutdown the plugin
         */
        virtual void Shutdown() = 0;

        /**
         * @brief Enable the plugin
         */
        virtual bool Enable() = 0;

        /**
         * @brief Disable the plugin
         */
        virtual void Disable() = 0;

        /**
         * @brief Get plugin settings
         */
        virtual std::unordered_map<std::string, std::string> GetSettings() const = 0;

        /**
         * @brief Apply plugin settings
         */
        virtual void ApplySettings(const std::unordered_map<std::string, std::string>& settings) = 0;
    };

    /**
     * @brief Interface for preview handler plugins
     */
    class IPreviewPlugin : public virtual IPlugin
    {
    public:
        /**
         * @brief Check if this plugin can preview the given file
         */
        virtual bool CanPreview(const std::filesystem::path& path) const = 0;

        /**
         * @brief Get preview data for a file
         * @param path File to preview
         * @param maxWidth Maximum width for preview
         * @param maxHeight Maximum height for preview
         * @return Preview data as bytes (format depends on plugin)
         */
        virtual std::vector<uint8_t> GetPreview(
            const std::filesystem::path& path,
            int maxWidth,
            int maxHeight) = 0;

        /**
         * @brief Get the MIME type of the preview output
         */
        virtual std::string GetPreviewMimeType() const = 0;
    };

    /**
     * @brief Interface for context menu plugins
     */
    class IContextMenuPlugin : public virtual IPlugin
    {
    public:
        struct MenuItem
        {
            std::string id;
            std::string label;
            std::string icon;       // Optional icon path
            std::string shortcut;   // Display shortcut
            bool separator = false; // Is this a separator
            bool enabled = true;
            std::vector<MenuItem> submenu;
        };

        /**
         * @brief Get menu items for the given selection
         */
        virtual std::vector<MenuItem> GetMenuItems(
            const std::vector<std::filesystem::path>& selectedPaths) = 0;

        /**
         * @brief Execute a menu action
         */
        virtual void ExecuteAction(
            const std::string& actionId,
            const std::vector<std::filesystem::path>& selectedPaths) = 0;
    };

    /**
     * @brief Interface for command provider plugins
     */
    class ICommandPlugin : public virtual IPlugin
    {
    public:
        struct Command
        {
            std::string id;
            std::string label;
            std::string description;
            std::string category;
            std::string shortcut;
            std::function<void()> action;
        };

        /**
         * @brief Get all commands provided by this plugin
         */
        virtual std::vector<Command> GetCommands() = 0;
    };

    /**
     * @brief Plugin load result
     */
    struct PluginLoadResult
    {
        bool success = false;
        std::string error;
        PluginInfo info;
    };

    /**
     * @brief Callback types for plugin events
     */
    using PluginLoadedCallback = std::function<void(const PluginInfo&)>;
    using PluginUnloadedCallback = std::function<void(const std::string& pluginId)>;
    using PluginErrorCallback = std::function<void(const std::string& pluginId, const std::string& error)>;

    /**
     * @brief Plugin manager for loading, managing, and coordinating plugins
     * 
     * Handles:
     * - DLL-based plugin loading
     * - Plugin lifecycle management
     * - Plugin discovery and enumeration
     * - Plugin settings persistence
     * - Security validation
     */
    class PluginManager
    {
    public:
        PluginManager();
        ~PluginManager();

        // Non-copyable, movable
        PluginManager(const PluginManager&) = delete;
        PluginManager& operator=(const PluginManager&) = delete;
        PluginManager(PluginManager&&) noexcept;
        PluginManager& operator=(PluginManager&&) noexcept;

        /**
         * @brief Initialize the plugin manager
         * @param pluginDirectory Directory to scan for plugins
         */
        bool Initialize(const std::filesystem::path& pluginDirectory);

        /**
         * @brief Shutdown all plugins and cleanup
         */
        void Shutdown();

        // ============== Plugin Discovery ==============

        /**
         * @brief Scan plugin directory for available plugins
         */
        std::vector<PluginInfo> DiscoverPlugins();

        /**
         * @brief Refresh plugin list
         */
        void RefreshPluginList();

        /**
         * @brief Get all discovered plugins
         */
        std::vector<PluginInfo> GetAllPlugins() const;

        /**
         * @brief Get plugins by capability
         */
        std::vector<PluginInfo> GetPluginsByCapability(PluginCapability capability) const;

        /**
         * @brief Get plugin info by ID
         */
        std::optional<PluginInfo> GetPluginInfo(const std::string& pluginId) const;

        // ============== Plugin Loading ==============

        /**
         * @brief Load a specific plugin
         */
        PluginLoadResult LoadPlugin(const std::string& pluginId);

        /**
         * @brief Load a plugin from a specific path
         */
        PluginLoadResult LoadPluginFromPath(const std::filesystem::path& dllPath);

        /**
         * @brief Unload a plugin
         */
        bool UnloadPlugin(const std::string& pluginId);

        /**
         * @brief Load all enabled plugins
         */
        void LoadEnabledPlugins();

        /**
         * @brief Unload all plugins
         */
        void UnloadAllPlugins();

        // ============== Plugin State ==============

        /**
         * @brief Enable a plugin
         */
        bool EnablePlugin(const std::string& pluginId);

        /**
         * @brief Disable a plugin
         */
        bool DisablePlugin(const std::string& pluginId);

        /**
         * @brief Check if plugin is loaded
         */
        bool IsPluginLoaded(const std::string& pluginId) const;

        /**
         * @brief Check if plugin is enabled
         */
        bool IsPluginEnabled(const std::string& pluginId) const;

        /**
         * @brief Get plugin state
         */
        PluginState GetPluginState(const std::string& pluginId) const;

        // ============== Plugin Access ==============

        /**
         * @brief Get plugin instance
         */
        IPlugin* GetPlugin(const std::string& pluginId);

        /**
         * @brief Get all loaded preview plugins
         */
        std::vector<IPreviewPlugin*> GetPreviewPlugins();

        /**
         * @brief Get all loaded context menu plugins
         */
        std::vector<IContextMenuPlugin*> GetContextMenuPlugins();

        /**
         * @brief Get all loaded command plugins
         */
        std::vector<ICommandPlugin*> GetCommandPlugins();

        /**
         * @brief Find preview plugin for file extension
         */
        IPreviewPlugin* FindPreviewPlugin(const std::filesystem::path& path);

        // ============== Settings ==============

        /**
         * @brief Get plugin settings
         */
        PluginSettings GetPluginSettings(const std::string& pluginId) const;

        /**
         * @brief Save plugin settings
         */
        void SavePluginSettings(const PluginSettings& settings);

        /**
         * @brief Load all plugin settings from disk
         */
        void LoadSettings(const std::filesystem::path& settingsPath);

        /**
         * @brief Save all plugin settings to disk
         */
        void SaveSettings(const std::filesystem::path& settingsPath);

        // ============== Security ==============

        /**
         * @brief Validate plugin DLL signature
         */
        bool ValidatePluginSignature(const std::filesystem::path& dllPath);

        /**
         * @brief Set whether to require signed plugins
         */
        void SetRequireSignedPlugins(bool require);

        /**
         * @brief Add trusted plugin publisher
         */
        void AddTrustedPublisher(const std::string& publisherHash);

        // ============== Callbacks ==============

        /**
         * @brief Register callback for plugin loaded event
         */
        void OnPluginLoaded(PluginLoadedCallback callback);

        /**
         * @brief Register callback for plugin unloaded event
         */
        void OnPluginUnloaded(PluginUnloadedCallback callback);

        /**
         * @brief Register callback for plugin error event
         */
        void OnPluginError(PluginErrorCallback callback);

        // ============== Plugin Directory ==============

        /**
         * @brief Get plugin directory
         */
        std::filesystem::path GetPluginDirectory() const;

        /**
         * @brief Install plugin from file
         */
        bool InstallPlugin(const std::filesystem::path& pluginPackage);

        /**
         * @brief Uninstall plugin
         */
        bool UninstallPlugin(const std::string& pluginId);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    // ============== Plugin Export Macros ==============

    /**
     * @brief Macros for plugin DLL exports
     * 
     * Plugins must export these functions:
     * - OpacityPluginCreate(): Creates plugin instance
     * - OpacityPluginDestroy(IPlugin*): Destroys plugin instance
     * - OpacityPluginGetInfo(): Returns plugin metadata
     */

#ifdef _WIN32
#define OPACITY_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define OPACITY_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define OPACITY_DECLARE_PLUGIN(PluginClass) \
    OPACITY_PLUGIN_EXPORT opacity::core::IPlugin* OpacityPluginCreate() { \
        return new PluginClass(); \
    } \
    OPACITY_PLUGIN_EXPORT void OpacityPluginDestroy(opacity::core::IPlugin* plugin) { \
        delete plugin; \
    } \
    OPACITY_PLUGIN_EXPORT opacity::core::PluginInfo OpacityPluginGetInfo() { \
        PluginClass temp; \
        return temp.GetInfo(); \
    }

} // namespace opacity::core
