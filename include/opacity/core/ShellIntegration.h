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
    /**
     * @brief Command line argument types
     */
    enum class ArgType
    {
        Path,           // File or folder path
        Flag,           // Boolean flag (--flag)
        Option,         // Key-value option (--key=value)
        Command         // Subcommand (open, search, etc.)
    };

    /**
     * @brief Parsed command line argument
     */
    struct ParsedArg
    {
        ArgType type;
        std::string name;
        std::string value;
        std::filesystem::path path;  // For Path types
    };

    /**
     * @brief Command line parsing result
     */
    struct CommandLineResult
    {
        bool valid = false;
        std::string error;
        
        // Parsed values
        std::string command;                    // Main command (open, search, etc.)
        std::vector<std::filesystem::path> paths;
        std::unordered_map<std::string, std::string> options;
        std::vector<std::string> flags;
        
        // Quick accessors
        bool HasFlag(const std::string& flag) const;
        std::optional<std::string> GetOption(const std::string& key) const;
        std::filesystem::path GetFirstPath() const;
    };

    /**
     * @brief IPC message types for single-instance communication
     */
    enum class IpcMessageType
    {
        OpenPath,           // Open a path in existing instance
        NavigateTo,         // Navigate to a specific folder
        Search,             // Perform a search
        FocusWindow,        // Bring window to foreground
        ExecuteCommand,     // Execute a named command
        Shutdown            // Request application shutdown
    };

    /**
     * @brief IPC message structure
     */
    struct IpcMessage
    {
        IpcMessageType type;
        std::vector<std::string> args;
        
        std::string Serialize() const;
        static std::optional<IpcMessage> Deserialize(const std::string& data);
    };

    /**
     * @brief Shell integration configuration
     */
    struct ShellIntegrationConfig
    {
        // Context menu settings
        bool enableContextMenu = true;
        bool showOpenInOpacity = true;
        bool showSearchInOpacity = true;
        bool showCompareWith = true;
        
        // File associations
        bool registerAsDefaultBrowser = false;
        std::vector<std::string> associatedExtensions;
        
        // Send To menu
        bool addToSendTo = true;
        
        // Single instance
        bool enforceSingleInstance = true;
        std::string mutexName = "OpacityFileExplorerMutex";
        std::string pipeName = "OpacityIPC";
    };

    /**
     * @brief Callback type for IPC message handling
     */
    using IpcMessageHandler = std::function<void(const IpcMessage&)>;

    /**
     * @brief Shell integration manager for Windows
     * 
     * Handles:
     * - Windows Explorer context menu integration
     * - "Open with Opacity" registration
     * - Send To menu items
     * - Command line argument parsing
     * - Single-instance enforcement via named mutex
     * - IPC communication between instances
     */
    class ShellIntegration
    {
    public:
        ShellIntegration();
        ~ShellIntegration();

        // Non-copyable, movable
        ShellIntegration(const ShellIntegration&) = delete;
        ShellIntegration& operator=(const ShellIntegration&) = delete;
        ShellIntegration(ShellIntegration&&) noexcept;
        ShellIntegration& operator=(ShellIntegration&&) noexcept;

        /**
         * @brief Initialize with configuration
         */
        bool Initialize(const ShellIntegrationConfig& config);

        /**
         * @brief Shutdown and cleanup
         */
        void Shutdown();

        // ============== Context Menu Registration ==============

        /**
         * @brief Register context menu entries (requires admin rights)
         */
        bool RegisterContextMenu();

        /**
         * @brief Unregister context menu entries (requires admin rights)
         */
        bool UnregisterContextMenu();

        /**
         * @brief Check if context menu is registered
         */
        bool IsContextMenuRegistered() const;

        // ============== File Associations ==============

        /**
         * @brief Register as handler for specific extension
         */
        bool RegisterFileAssociation(const std::string& extension);

        /**
         * @brief Unregister file association
         */
        bool UnregisterFileAssociation(const std::string& extension);

        /**
         * @brief Register as default file browser (requires admin)
         */
        bool RegisterAsDefaultBrowser();

        /**
         * @brief Unregister as default browser
         */
        bool UnregisterAsDefaultBrowser();

        // ============== Send To Menu ==============

        /**
         * @brief Add "Opacity" to Send To menu
         */
        bool AddToSendToMenu();

        /**
         * @brief Remove from Send To menu
         */
        bool RemoveFromSendToMenu();

        /**
         * @brief Check if present in Send To menu
         */
        bool IsInSendToMenu() const;

        // ============== Command Line ==============

        /**
         * @brief Parse command line arguments
         */
        CommandLineResult ParseCommandLine(int argc, char* argv[]);

        /**
         * @brief Parse command line from string
         */
        CommandLineResult ParseCommandLine(const std::string& cmdLine);

        /**
         * @brief Parse Windows command line (GetCommandLineW)
         */
        CommandLineResult ParseWindowsCommandLine();

        /**
         * @brief Get usage/help text
         */
        std::string GetUsageText() const;

        /**
         * @brief Get version string
         */
        std::string GetVersionText() const;

        // ============== Single Instance ==============

        /**
         * @brief Check if this is the first (primary) instance
         */
        bool IsPrimaryInstance();

        /**
         * @brief Send message to primary instance
         */
        bool SendToPrimaryInstance(const IpcMessage& message);

        /**
         * @brief Start listening for IPC messages
         */
        bool StartIpcListener(IpcMessageHandler handler);

        /**
         * @brief Stop IPC listener
         */
        void StopIpcListener();

        /**
         * @brief Check if IPC listener is running
         */
        bool IsIpcListenerRunning() const;

        // ============== Admin Rights ==============

        /**
         * @brief Check if running with admin rights
         */
        static bool IsRunningAsAdmin();

        /**
         * @brief Request elevation (restart as admin)
         */
        static bool RequestElevation(const std::string& parameters = "");

        /**
         * @brief Get executable path
         */
        static std::filesystem::path GetExecutablePath();

        // ============== Configuration ==============

        /**
         * @brief Get current configuration
         */
        const ShellIntegrationConfig& GetConfig() const { return config_; }

        /**
         * @brief Update configuration
         */
        void SetConfig(const ShellIntegrationConfig& config);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;

        ShellIntegrationConfig config_;
        bool initialized_ = false;
    };

    /**
     * @brief Helper to create shell integration easily
     */
    class ShellIntegrationBuilder
    {
    public:
        ShellIntegrationBuilder& WithContextMenu(bool enable = true);
        ShellIntegrationBuilder& WithSendTo(bool enable = true);
        ShellIntegrationBuilder& WithSingleInstance(bool enable = true);
        ShellIntegrationBuilder& WithMutexName(const std::string& name);
        ShellIntegrationBuilder& WithPipeName(const std::string& name);
        ShellIntegrationBuilder& WithFileAssociations(const std::vector<std::string>& exts);
        
        ShellIntegrationConfig Build() const;
        std::unique_ptr<ShellIntegration> Create() const;

    private:
        ShellIntegrationConfig config_;
    };

} // namespace opacity::core
