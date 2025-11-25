#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>

namespace opacity::ui
{
    /**
     * @brief Modifier key flags
     */
    enum class ModifierKeys : unsigned int
    {
        None = 0,
        Ctrl = 1 << 0,
        Shift = 1 << 1,
        Alt = 1 << 2,
        Super = 1 << 3  // Windows key
    };

    inline ModifierKeys operator|(ModifierKeys a, ModifierKeys b)
    {
        return static_cast<ModifierKeys>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));
    }

    inline ModifierKeys operator&(ModifierKeys a, ModifierKeys b)
    {
        return static_cast<ModifierKeys>(static_cast<unsigned int>(a) & static_cast<unsigned int>(b));
    }

    inline bool operator!(ModifierKeys m)
    {
        return static_cast<unsigned int>(m) == 0;
    }

    /**
     * @brief Represents a keyboard shortcut
     */
    struct Keybind
    {
        int key = 0;                        // ImGuiKey value
        ModifierKeys modifiers = ModifierKeys::None;

        bool operator==(const Keybind& other) const
        {
            return key == other.key && modifiers == other.modifiers;
        }

        bool operator!=(const Keybind& other) const
        {
            return !(*this == other);
        }

        /**
         * @brief Check if this keybind is currently pressed
         */
        bool IsPressed() const;

        /**
         * @brief Get string representation (e.g., "Ctrl+Shift+N")
         */
        std::string ToString() const;

        /**
         * @brief Parse from string representation
         */
        static std::optional<Keybind> FromString(const std::string& str);

        /**
         * @brief Check if this keybind is valid
         */
        bool IsValid() const { return key != 0; }
    };

    /**
     * @brief Command categories for organization
     */
    enum class CommandCategory
    {
        Navigation,
        FileOperations,
        Selection,
        View,
        Tabs,
        Panes,
        Search,
        Help,
        Custom
    };

    /**
     * @brief Definition of a command that can be bound to a key
     */
    struct CommandDef
    {
        std::string id;                     // Unique identifier (e.g., "file.copy")
        std::string name;                   // Display name (e.g., "Copy")
        std::string description;            // Help text
        CommandCategory category = CommandCategory::FileOperations;
        Keybind default_keybind;            // Default keyboard shortcut
        bool allow_rebind = true;           // Whether user can rebind
    };

    /**
     * @brief Manages keyboard shortcuts and command bindings
     * 
     * Features:
     * - Rebindable hotkeys
     * - Context-sensitive shortcuts
     * - Command palette support
     * - Import/export keybind configurations
     */
    class KeybindManager
    {
    public:
        using CommandCallback = std::function<void()>;
        using ConditionalCallback = std::function<bool()>;  // Returns true if handled

        KeybindManager();
        ~KeybindManager() = default;

        /**
         * @brief Initialize with default keybinds
         */
        void Initialize();

        /**
         * @brief Register a new command
         * @param def Command definition
         * @param callback Function to call when command is executed
         * @param condition Optional condition for when command is available
         */
        void RegisterCommand(
            const CommandDef& def,
            CommandCallback callback,
            ConditionalCallback condition = nullptr);

        /**
         * @brief Unregister a command
         */
        void UnregisterCommand(const std::string& command_id);

        /**
         * @brief Execute a command by ID
         */
        bool ExecuteCommand(const std::string& command_id);

        /**
         * @brief Set keybind for a command
         */
        bool SetKeybind(const std::string& command_id, const Keybind& keybind);

        /**
         * @brief Get keybind for a command
         */
        std::optional<Keybind> GetKeybind(const std::string& command_id) const;

        /**
         * @brief Reset command to default keybind
         */
        void ResetKeybind(const std::string& command_id);

        /**
         * @brief Reset all keybinds to defaults
         */
        void ResetAllKeybinds();

        /**
         * @brief Get command for a keybind
         */
        std::optional<std::string> GetCommandForKeybind(const Keybind& keybind) const;

        /**
         * @brief Check if a keybind conflicts with existing bindings
         */
        bool HasConflict(const Keybind& keybind, const std::string& exclude_command = "") const;

        /**
         * @brief Process keyboard input and execute matching commands
         * @return true if a command was executed
         */
        bool ProcessInput();

        /**
         * @brief Get all registered commands
         */
        std::vector<CommandDef> GetAllCommands() const;

        /**
         * @brief Get commands by category
         */
        std::vector<CommandDef> GetCommandsByCategory(CommandCategory category) const;

        /**
         * @brief Search commands by name or description
         */
        std::vector<CommandDef> SearchCommands(const std::string& query) const;

        /**
         * @brief Get shortcut help text for display
         */
        std::string GetShortcutHelpText(const std::string& command_id) const;

        // Persistence
        bool LoadKeybinds(const std::string& path);
        bool SaveKeybinds(const std::string& path) const;
        bool ExportKeybinds(const std::string& path) const;
        bool ImportKeybinds(const std::string& path);

        /**
         * @brief Enable/disable all keyboard processing
         */
        void SetEnabled(bool enabled) { enabled_ = enabled; }
        bool IsEnabled() const { return enabled_; }

        /**
         * @brief Render keybind configuration UI
         */
        void RenderSettingsUI();

        /**
         * @brief Render command palette
         * @return true if palette consumed input
         */
        bool RenderCommandPalette();

        /**
         * @brief Show command palette
         */
        void ShowCommandPalette() { show_command_palette_ = true; }

    private:
        struct CommandBinding
        {
            CommandDef definition;
            Keybind current_keybind;
            CommandCallback callback;
            ConditionalCallback condition;
        };

        void RegisterDefaultCommands();
        bool MatchesCurrentInput(const Keybind& keybind) const;

        std::unordered_map<std::string, CommandBinding> commands_;
        bool enabled_ = true;
        bool show_command_palette_ = false;
        char palette_search_[256] = "";

        // For detecting keybind input in settings
        bool listening_for_keybind_ = false;
        std::string keybind_target_command_;
    };

} // namespace opacity::ui
