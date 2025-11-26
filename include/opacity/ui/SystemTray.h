#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opacity::ui
{
    /**
     * @brief Tray icon click type
     */
    enum class TrayClickType
    {
        LeftClick,
        RightClick,
        DoubleClick,
        MiddleClick
    };

    /**
     * @brief Notification urgency level
     */
    enum class NotificationUrgency
    {
        Low,        // Silent notification
        Normal,     // Standard notification
        Critical    // Important notification with sound
    };

    /**
     * @brief Tray menu item structure
     */
    struct TrayMenuItem
    {
        std::string id;
        std::string label;
        std::string icon;           // Optional icon path
        bool enabled = true;
        bool checked = false;
        bool separator = false;
        std::vector<TrayMenuItem> submenu;
    };

    /**
     * @brief Tray notification structure
     */
    struct TrayNotification
    {
        std::string title;
        std::string message;
        std::string iconPath;       // Optional custom icon
        NotificationUrgency urgency = NotificationUrgency::Normal;
        int timeoutMs = 5000;       // 0 = no timeout
        std::function<void()> onClick;
    };

    /**
     * @brief Background operation status for tray display
     */
    struct BackgroundOperationStatus
    {
        std::string id;
        std::string description;
        double progress = 0.0;      // 0.0 to 1.0, -1 for indeterminate
        bool cancellable = true;
    };

    /**
     * @brief Quick action for tray menu
     */
    struct QuickAction
    {
        std::string id;
        std::string label;
        std::string shortcut;
        std::function<void()> action;
    };

    /**
     * @brief System tray configuration
     */
    struct SystemTrayConfig
    {
        std::string tooltip = "Opacity File Explorer";
        std::string iconPath;               // Path to tray icon
        bool showOnMinimize = true;         // Show tray icon when minimized
        bool minimizeToTray = true;         // Minimize to tray instead of taskbar
        bool closeToTray = false;           // Close to tray instead of exit
        bool showNotifications = true;
        bool showOperationProgress = true;
        bool startMinimized = false;
        std::vector<QuickAction> quickActions;
    };

    /**
     * @brief Callback types for tray events
     */
    using TrayClickCallback = std::function<void(TrayClickType)>;
    using TrayMenuCallback = std::function<void(const std::string& itemId)>;
    using TrayNotificationCallback = std::function<void()>;

    /**
     * @brief System tray integration manager
     * 
     * Handles:
     * - Minimize to tray functionality
     * - Tray icon and tooltip management
     * - Context menu from tray
     * - Desktop notifications
     * - Background operation status display
     * - Quick actions
     */
    class SystemTray
    {
    public:
        SystemTray();
        ~SystemTray();

        // Non-copyable, movable
        SystemTray(const SystemTray&) = delete;
        SystemTray& operator=(const SystemTray&) = delete;
        SystemTray(SystemTray&&) noexcept;
        SystemTray& operator=(SystemTray&&) noexcept;

        /**
         * @brief Initialize the system tray
         * @param hwnd Main window handle
         * @param config Tray configuration
         */
        bool Initialize(void* hwnd, const SystemTrayConfig& config);

        /**
         * @brief Shutdown and remove tray icon
         */
        void Shutdown();

        /**
         * @brief Check if tray is initialized
         */
        bool IsInitialized() const;

        // ============== Icon Management ==============

        /**
         * @brief Show the tray icon
         */
        bool Show();

        /**
         * @brief Hide the tray icon
         */
        bool Hide();

        /**
         * @brief Check if tray icon is visible
         */
        bool IsVisible() const;

        /**
         * @brief Update tray icon
         */
        bool SetIcon(const std::string& iconPath);

        /**
         * @brief Set icon from resource ID
         */
        bool SetIconFromResource(int resourceId);

        /**
         * @brief Update tooltip text
         */
        void SetTooltip(const std::string& tooltip);

        /**
         * @brief Flash the tray icon for attention
         */
        void FlashIcon(int count = 3, int intervalMs = 500);

        // ============== Menu ==============

        /**
         * @brief Set tray context menu items
         */
        void SetMenuItems(const std::vector<TrayMenuItem>& items);

        /**
         * @brief Add quick action to menu
         */
        void AddQuickAction(const QuickAction& action);

        /**
         * @brief Remove quick action
         */
        void RemoveQuickAction(const std::string& actionId);

        /**
         * @brief Clear all quick actions
         */
        void ClearQuickActions();

        /**
         * @brief Show context menu at current cursor position
         */
        void ShowContextMenu();

        // ============== Notifications ==============

        /**
         * @brief Show a notification balloon
         */
        bool ShowNotification(const TrayNotification& notification);

        /**
         * @brief Show a simple notification
         */
        bool ShowNotification(const std::string& title, const std::string& message,
                             NotificationUrgency urgency = NotificationUrgency::Normal);

        /**
         * @brief Hide current notification
         */
        void HideNotification();

        /**
         * @brief Enable/disable notifications
         */
        void SetNotificationsEnabled(bool enabled);

        /**
         * @brief Check if notifications are enabled
         */
        bool AreNotificationsEnabled() const;

        // ============== Background Operations ==============

        /**
         * @brief Update background operation status
         */
        void UpdateOperationStatus(const BackgroundOperationStatus& status);

        /**
         * @brief Remove operation status
         */
        void RemoveOperationStatus(const std::string& operationId);

        /**
         * @brief Clear all operation statuses
         */
        void ClearOperationStatuses();

        /**
         * @brief Get all active operations
         */
        std::vector<BackgroundOperationStatus> GetActiveOperations() const;

        // ============== Window Management ==============

        /**
         * @brief Minimize main window to tray
         */
        void MinimizeToTray();

        /**
         * @brief Restore main window from tray
         */
        void RestoreFromTray();

        /**
         * @brief Check if window is minimized to tray
         */
        bool IsMinimizedToTray() const;

        /**
         * @brief Set minimize to tray behavior
         */
        void SetMinimizeToTray(bool enable);

        /**
         * @brief Set close to tray behavior
         */
        void SetCloseToTray(bool enable);

        // ============== Event Callbacks ==============

        /**
         * @brief Set callback for tray icon clicks
         */
        void OnClick(TrayClickCallback callback);

        /**
         * @brief Set callback for menu item selection
         */
        void OnMenuItemSelected(TrayMenuCallback callback);

        /**
         * @brief Set callback for notification click
         */
        void OnNotificationClick(TrayNotificationCallback callback);

        // ============== Configuration ==============

        /**
         * @brief Get current configuration
         */
        const SystemTrayConfig& GetConfig() const;

        /**
         * @brief Update configuration
         */
        void SetConfig(const SystemTrayConfig& config);

        // ============== Windows Message Processing ==============

        /**
         * @brief Process Windows message for tray
         * @return true if message was handled
         */
        bool ProcessMessage(unsigned int message, uintptr_t wParam, intptr_t lParam);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace opacity::ui
