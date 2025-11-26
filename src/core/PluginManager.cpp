#include "opacity/core/PluginManager.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wintrust.h>
#include <softpub.h>
#pragma comment(lib, "wintrust.lib")
#endif

namespace opacity::core
{
    using json = nlohmann::json;

    // Plugin function pointer types
    using CreatePluginFunc = IPlugin* (*)();
    using DestroyPluginFunc = void (*)(IPlugin*);
    using GetInfoFunc = PluginInfo (*)();

    /**
     * @brief Loaded plugin data
     */
    struct LoadedPlugin
    {
#ifdef _WIN32
        HMODULE module = nullptr;
#else
        void* module = nullptr;
#endif
        IPlugin* instance = nullptr;
        CreatePluginFunc createFunc = nullptr;
        DestroyPluginFunc destroyFunc = nullptr;
        PluginInfo info;
    };

    // ============== PluginManager::Impl ==============

    class PluginManager::Impl
    {
    public:
        std::filesystem::path pluginDirectory_;
        std::filesystem::path settingsPath_;
        
        std::unordered_map<std::string, PluginInfo> discoveredPlugins_;
        std::unordered_map<std::string, LoadedPlugin> loadedPlugins_;
        std::unordered_map<std::string, PluginSettings> pluginSettings_;
        
        std::vector<PluginLoadedCallback> loadedCallbacks_;
        std::vector<PluginUnloadedCallback> unloadedCallbacks_;
        std::vector<PluginErrorCallback> errorCallbacks_;
        
        std::vector<std::string> trustedPublishers_;
        bool requireSignedPlugins_ = false;
        
        mutable std::mutex mutex_;

        void NotifyPluginLoaded(const PluginInfo& info)
        {
            for (auto& callback : loadedCallbacks_) {
                if (callback) callback(info);
            }
        }

        void NotifyPluginUnloaded(const std::string& pluginId)
        {
            for (auto& callback : unloadedCallbacks_) {
                if (callback) callback(pluginId);
            }
        }

        void NotifyPluginError(const std::string& pluginId, const std::string& error)
        {
            for (auto& callback : errorCallbacks_) {
                if (callback) callback(pluginId, error);
            }
        }

#ifdef _WIN32
        bool LoadPluginDll(const std::filesystem::path& dllPath, LoadedPlugin& plugin)
        {
            plugin.module = LoadLibraryW(dllPath.wstring().c_str());
            if (!plugin.module) {
                DWORD error = GetLastError();
                Logger::Get()->error("PluginManager: Failed to load DLL {}, error: {}", 
                    dllPath.string(), error);
                return false;
            }

            plugin.createFunc = reinterpret_cast<CreatePluginFunc>(
                GetProcAddress(plugin.module, "OpacityPluginCreate"));
            plugin.destroyFunc = reinterpret_cast<DestroyPluginFunc>(
                GetProcAddress(plugin.module, "OpacityPluginDestroy"));
            auto getInfoFunc = reinterpret_cast<GetInfoFunc>(
                GetProcAddress(plugin.module, "OpacityPluginGetInfo"));

            if (!plugin.createFunc || !plugin.destroyFunc || !getInfoFunc) {
                Logger::Get()->error("PluginManager: DLL missing required exports: {}", 
                    dllPath.string());
                FreeLibrary(plugin.module);
                plugin.module = nullptr;
                return false;
            }

            // Get plugin info
            plugin.info = getInfoFunc();
            plugin.info.dllPath = dllPath;
            plugin.info.state = PluginState::Loaded;

            return true;
        }

        void UnloadPluginDll(LoadedPlugin& plugin)
        {
            if (plugin.instance) {
                plugin.instance->Shutdown();
                if (plugin.destroyFunc) {
                    plugin.destroyFunc(plugin.instance);
                }
                plugin.instance = nullptr;
            }

            if (plugin.module) {
                FreeLibrary(plugin.module);
                plugin.module = nullptr;
            }
        }
#endif
    };

    // ============== PluginManager ==============

    PluginManager::PluginManager()
        : impl_(std::make_unique<Impl>())
    {}

    PluginManager::~PluginManager()
    {
        Shutdown();
    }

    PluginManager::PluginManager(PluginManager&&) noexcept = default;
    PluginManager& PluginManager::operator=(PluginManager&&) noexcept = default;

    bool PluginManager::Initialize(const std::filesystem::path& pluginDirectory)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        impl_->pluginDirectory_ = pluginDirectory;

        // Create plugin directory if it doesn't exist
        std::error_code ec;
        if (!std::filesystem::exists(pluginDirectory, ec)) {
            std::filesystem::create_directories(pluginDirectory, ec);
            if (ec) {
                Logger::Get()->error("PluginManager: Failed to create plugin directory: {}", 
                    ec.message());
                return false;
            }
        }

        Logger::Get()->info("PluginManager: Initialized with directory: {}", 
            pluginDirectory.string());
        return true;
    }

    void PluginManager::Shutdown()
    {
        UnloadAllPlugins();
        impl_->discoveredPlugins_.clear();
        impl_->pluginSettings_.clear();
    }

    std::vector<PluginInfo> PluginManager::DiscoverPlugins()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        impl_->discoveredPlugins_.clear();
        std::vector<PluginInfo> result;

        std::error_code ec;
        if (!std::filesystem::exists(impl_->pluginDirectory_, ec)) {
            return result;
        }

        for (const auto& entry : std::filesystem::directory_iterator(impl_->pluginDirectory_, ec)) {
            if (!entry.is_regular_file()) continue;
            
            const auto& path = entry.path();
            if (path.extension() != ".dll" && path.extension() != ".so") continue;

            // Try to load and get info without fully initializing
            LoadedPlugin tempPlugin;
#ifdef _WIN32
            if (impl_->LoadPluginDll(path, tempPlugin)) {
                PluginInfo info = tempPlugin.info;
                impl_->discoveredPlugins_[info.id] = info;
                result.push_back(info);
                
                // Unload - we just wanted the info
                impl_->UnloadPluginDll(tempPlugin);
            }
#endif
        }

        Logger::Get()->info("PluginManager: Discovered {} plugins", result.size());
        return result;
    }

    void PluginManager::RefreshPluginList()
    {
        DiscoverPlugins();
    }

    std::vector<PluginInfo> PluginManager::GetAllPlugins() const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        std::vector<PluginInfo> result;
        result.reserve(impl_->discoveredPlugins_.size());
        
        for (const auto& [id, info] : impl_->discoveredPlugins_) {
            result.push_back(info);
        }
        
        return result;
    }

    std::vector<PluginInfo> PluginManager::GetPluginsByCapability(PluginCapability capability) const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        std::vector<PluginInfo> result;
        
        for (const auto& [id, info] : impl_->discoveredPlugins_) {
            if (HasCapability(info.capabilities, capability)) {
                result.push_back(info);
            }
        }
        
        return result;
    }

    std::optional<PluginInfo> PluginManager::GetPluginInfo(const std::string& pluginId) const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        // Check loaded plugins first
        auto loadedIt = impl_->loadedPlugins_.find(pluginId);
        if (loadedIt != impl_->loadedPlugins_.end()) {
            return loadedIt->second.info;
        }
        
        // Check discovered plugins
        auto discoveredIt = impl_->discoveredPlugins_.find(pluginId);
        if (discoveredIt != impl_->discoveredPlugins_.end()) {
            return discoveredIt->second;
        }
        
        return std::nullopt;
    }

    PluginLoadResult PluginManager::LoadPlugin(const std::string& pluginId)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        PluginLoadResult result;

        // Check if already loaded
        if (impl_->loadedPlugins_.find(pluginId) != impl_->loadedPlugins_.end()) {
            result.success = true;
            result.info = impl_->loadedPlugins_[pluginId].info;
            return result;
        }

        // Find plugin info
        auto it = impl_->discoveredPlugins_.find(pluginId);
        if (it == impl_->discoveredPlugins_.end()) {
            result.error = "Plugin not found: " + pluginId;
            impl_->NotifyPluginError(pluginId, result.error);
            return result;
        }

        return LoadPluginFromPath(it->second.dllPath);
    }

    PluginLoadResult PluginManager::LoadPluginFromPath(const std::filesystem::path& dllPath)
    {
        PluginLoadResult result;

#ifdef _WIN32
        // Validate signature if required
        if (impl_->requireSignedPlugins_ && !ValidatePluginSignature(dllPath)) {
            result.error = "Plugin signature validation failed";
            return result;
        }

        LoadedPlugin plugin;
        if (!impl_->LoadPluginDll(dllPath, plugin)) {
            result.error = "Failed to load plugin DLL";
            return result;
        }

        // Check API compatibility
        if (!plugin.info.IsCompatible()) {
            result.error = "Plugin API version incompatible";
            impl_->UnloadPluginDll(plugin);
            return result;
        }

        // Create instance
        plugin.instance = plugin.createFunc();
        if (!plugin.instance) {
            result.error = "Failed to create plugin instance";
            impl_->UnloadPluginDll(plugin);
            return result;
        }

        // Initialize plugin
        plugin.info.state = PluginState::Initializing;
        if (!plugin.instance->Initialize(this)) {
            result.error = "Plugin initialization failed";
            plugin.info.state = PluginState::Error;
            plugin.info.errorMessage = result.error;
            impl_->UnloadPluginDll(plugin);
            return result;
        }

        plugin.info.state = PluginState::Active;

        // Store loaded plugin
        impl_->loadedPlugins_[plugin.info.id] = std::move(plugin);
        impl_->discoveredPlugins_[plugin.info.id] = impl_->loadedPlugins_[plugin.info.id].info;

        result.success = true;
        result.info = impl_->loadedPlugins_[plugin.info.id].info;

        impl_->NotifyPluginLoaded(result.info);
        Logger::Get()->info("PluginManager: Loaded plugin: {}", result.info.name);
#else
        result.error = "Plugin loading not implemented for this platform";
#endif

        return result;
    }

    bool PluginManager::UnloadPlugin(const std::string& pluginId)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);

        auto it = impl_->loadedPlugins_.find(pluginId);
        if (it == impl_->loadedPlugins_.end()) {
            return false;
        }

#ifdef _WIN32
        impl_->UnloadPluginDll(it->second);
#endif

        // Update discovered plugin state
        if (impl_->discoveredPlugins_.find(pluginId) != impl_->discoveredPlugins_.end()) {
            impl_->discoveredPlugins_[pluginId].state = PluginState::Unloaded;
        }

        impl_->loadedPlugins_.erase(it);
        impl_->NotifyPluginUnloaded(pluginId);
        
        Logger::Get()->info("PluginManager: Unloaded plugin: {}", pluginId);
        return true;
    }

    void PluginManager::LoadEnabledPlugins()
    {
        auto plugins = GetAllPlugins();
        
        for (const auto& plugin : plugins) {
            auto settingsIt = impl_->pluginSettings_.find(plugin.id);
            bool enabled = true;
            
            if (settingsIt != impl_->pluginSettings_.end()) {
                enabled = settingsIt->second.enabled;
            }
            
            if (enabled) {
                LoadPlugin(plugin.id);
            }
        }
    }

    void PluginManager::UnloadAllPlugins()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);

        for (auto& [id, plugin] : impl_->loadedPlugins_) {
#ifdef _WIN32
            impl_->UnloadPluginDll(plugin);
#endif
        }
        
        impl_->loadedPlugins_.clear();
        Logger::Get()->info("PluginManager: Unloaded all plugins");
    }

    bool PluginManager::EnablePlugin(const std::string& pluginId)
    {
        auto it = impl_->loadedPlugins_.find(pluginId);
        if (it == impl_->loadedPlugins_.end()) {
            // Try to load it first
            auto result = LoadPlugin(pluginId);
            return result.success;
        }

        if (it->second.instance) {
            return it->second.instance->Enable();
        }
        return false;
    }

    bool PluginManager::DisablePlugin(const std::string& pluginId)
    {
        auto it = impl_->loadedPlugins_.find(pluginId);
        if (it == impl_->loadedPlugins_.end()) {
            return false;
        }

        if (it->second.instance) {
            it->second.instance->Disable();
            it->second.info.state = PluginState::Disabled;
            return true;
        }
        return false;
    }

    bool PluginManager::IsPluginLoaded(const std::string& pluginId) const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        return impl_->loadedPlugins_.find(pluginId) != impl_->loadedPlugins_.end();
    }

    bool PluginManager::IsPluginEnabled(const std::string& pluginId) const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        auto it = impl_->loadedPlugins_.find(pluginId);
        if (it == impl_->loadedPlugins_.end()) {
            return false;
        }
        
        return it->second.info.state == PluginState::Active;
    }

    PluginState PluginManager::GetPluginState(const std::string& pluginId) const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        auto it = impl_->loadedPlugins_.find(pluginId);
        if (it != impl_->loadedPlugins_.end()) {
            return it->second.info.state;
        }
        
        auto discovered = impl_->discoveredPlugins_.find(pluginId);
        if (discovered != impl_->discoveredPlugins_.end()) {
            return discovered->second.state;
        }
        
        return PluginState::Unloaded;
    }

    IPlugin* PluginManager::GetPlugin(const std::string& pluginId)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        auto it = impl_->loadedPlugins_.find(pluginId);
        if (it != impl_->loadedPlugins_.end()) {
            return it->second.instance;
        }
        return nullptr;
    }

    std::vector<IPreviewPlugin*> PluginManager::GetPreviewPlugins()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        std::vector<IPreviewPlugin*> result;
        for (auto& [id, plugin] : impl_->loadedPlugins_) {
            if (HasCapability(plugin.info.capabilities, PluginCapability::PreviewHandler)) {
                auto* previewPlugin = dynamic_cast<IPreviewPlugin*>(plugin.instance);
                if (previewPlugin) {
                    result.push_back(previewPlugin);
                }
            }
        }
        return result;
    }

    std::vector<IContextMenuPlugin*> PluginManager::GetContextMenuPlugins()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        std::vector<IContextMenuPlugin*> result;
        for (auto& [id, plugin] : impl_->loadedPlugins_) {
            if (HasCapability(plugin.info.capabilities, PluginCapability::ContextMenu)) {
                auto* contextPlugin = dynamic_cast<IContextMenuPlugin*>(plugin.instance);
                if (contextPlugin) {
                    result.push_back(contextPlugin);
                }
            }
        }
        return result;
    }

    std::vector<ICommandPlugin*> PluginManager::GetCommandPlugins()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        std::vector<ICommandPlugin*> result;
        for (auto& [id, plugin] : impl_->loadedPlugins_) {
            if (HasCapability(plugin.info.capabilities, PluginCapability::CommandProvider)) {
                auto* cmdPlugin = dynamic_cast<ICommandPlugin*>(plugin.instance);
                if (cmdPlugin) {
                    result.push_back(cmdPlugin);
                }
            }
        }
        return result;
    }

    IPreviewPlugin* PluginManager::FindPreviewPlugin(const std::filesystem::path& path)
    {
        auto plugins = GetPreviewPlugins();
        for (auto* plugin : plugins) {
            if (plugin->CanPreview(path)) {
                return plugin;
            }
        }
        return nullptr;
    }

    PluginSettings PluginManager::GetPluginSettings(const std::string& pluginId) const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        auto it = impl_->pluginSettings_.find(pluginId);
        if (it != impl_->pluginSettings_.end()) {
            return it->second;
        }
        
        return PluginSettings{pluginId, true, {}};
    }

    void PluginManager::SavePluginSettings(const PluginSettings& settings)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->pluginSettings_[settings.pluginId] = settings;
        
        // Apply to loaded plugin if available
        auto it = impl_->loadedPlugins_.find(settings.pluginId);
        if (it != impl_->loadedPlugins_.end() && it->second.instance) {
            it->second.instance->ApplySettings(settings.settings);
        }
    }

    void PluginManager::LoadSettings(const std::filesystem::path& settingsPath)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->settingsPath_ = settingsPath;

        std::error_code ec;
        if (!std::filesystem::exists(settingsPath, ec)) {
            return;
        }

        try {
            std::ifstream file(settingsPath);
            json j = json::parse(file);

            for (const auto& [id, data] : j.items()) {
                PluginSettings settings;
                settings.pluginId = id;
                settings.enabled = data.value("enabled", true);
                
                if (data.contains("settings")) {
                    for (const auto& [key, value] : data["settings"].items()) {
                        settings.settings[key] = value.get<std::string>();
                    }
                }
                
                impl_->pluginSettings_[id] = settings;
            }

            Logger::Get()->info("PluginManager: Loaded settings for {} plugins", 
                impl_->pluginSettings_.size());
        }
        catch (const std::exception& e) {
            Logger::Get()->error("PluginManager: Failed to load settings: {}", e.what());
        }
    }

    void PluginManager::SaveSettings(const std::filesystem::path& settingsPath)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);

        try {
            json j;
            
            for (const auto& [id, settings] : impl_->pluginSettings_) {
                j[id]["enabled"] = settings.enabled;
                
                json settingsJson;
                for (const auto& [key, value] : settings.settings) {
                    settingsJson[key] = value;
                }
                j[id]["settings"] = settingsJson;
            }

            std::ofstream file(settingsPath);
            file << j.dump(4);

            Logger::Get()->info("PluginManager: Saved settings for {} plugins", 
                impl_->pluginSettings_.size());
        }
        catch (const std::exception& e) {
            Logger::Get()->error("PluginManager: Failed to save settings: {}", e.what());
        }
    }

    bool PluginManager::ValidatePluginSignature(const std::filesystem::path& dllPath)
    {
#ifdef _WIN32
        WINTRUST_FILE_INFO fileInfo = {};
        fileInfo.cbStruct = sizeof(fileInfo);
        fileInfo.pcwszFilePath = dllPath.wstring().c_str();

        GUID policyGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

        WINTRUST_DATA trustData = {};
        trustData.cbStruct = sizeof(trustData);
        trustData.dwUIChoice = WTD_UI_NONE;
        trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
        trustData.dwUnionChoice = WTD_CHOICE_FILE;
        trustData.pFile = &fileInfo;
        trustData.dwStateAction = WTD_STATEACTION_VERIFY;

        LONG result = WinVerifyTrust(nullptr, &policyGuid, &trustData);

        // Cleanup
        trustData.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, &policyGuid, &trustData);

        return result == ERROR_SUCCESS;
#else
        return true;  // No signature verification on non-Windows
#endif
    }

    void PluginManager::SetRequireSignedPlugins(bool require)
    {
        impl_->requireSignedPlugins_ = require;
    }

    void PluginManager::AddTrustedPublisher(const std::string& publisherHash)
    {
        impl_->trustedPublishers_.push_back(publisherHash);
    }

    void PluginManager::OnPluginLoaded(PluginLoadedCallback callback)
    {
        impl_->loadedCallbacks_.push_back(callback);
    }

    void PluginManager::OnPluginUnloaded(PluginUnloadedCallback callback)
    {
        impl_->unloadedCallbacks_.push_back(callback);
    }

    void PluginManager::OnPluginError(PluginErrorCallback callback)
    {
        impl_->errorCallbacks_.push_back(callback);
    }

    std::filesystem::path PluginManager::GetPluginDirectory() const
    {
        return impl_->pluginDirectory_;
    }

    bool PluginManager::InstallPlugin(const std::filesystem::path& pluginPackage)
    {
        // TODO: Implement plugin package installation (ZIP extraction, etc.)
        Logger::Get()->warn("PluginManager: InstallPlugin not yet implemented");
        return false;
    }

    bool PluginManager::UninstallPlugin(const std::string& pluginId)
    {
        // Unload first
        UnloadPlugin(pluginId);

        std::lock_guard<std::mutex> lock(impl_->mutex_);

        auto it = impl_->discoveredPlugins_.find(pluginId);
        if (it == impl_->discoveredPlugins_.end()) {
            return false;
        }

        std::error_code ec;
        std::filesystem::remove(it->second.dllPath, ec);
        
        if (ec) {
            Logger::Get()->error("PluginManager: Failed to delete plugin file: {}", ec.message());
            return false;
        }

        impl_->discoveredPlugins_.erase(it);
        impl_->pluginSettings_.erase(pluginId);

        Logger::Get()->info("PluginManager: Uninstalled plugin: {}", pluginId);
        return true;
    }

} // namespace opacity::core
