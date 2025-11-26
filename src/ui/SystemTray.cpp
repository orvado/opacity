#include "opacity/ui/SystemTray.h"
#include "opacity/core/Logger.h"

#include <atomic>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <strsafe.h>

#define WM_TRAYICON (WM_USER + 1)
#endif

namespace opacity::ui
{
    using namespace opacity::core;

    class SystemTray::Impl
    {
    public:
#ifdef _WIN32
        HWND hwnd_ = nullptr;
        NOTIFYICONDATAW nid_ = {};
        HMENU hMenu_ = nullptr;
        HICON hIcon_ = nullptr;
        bool iconAdded_ = false;
#endif

        SystemTrayConfig config_;
        std::vector<TrayMenuItem> menuItems_;
        std::vector<BackgroundOperationStatus> operations_;
        
        TrayClickCallback clickCallback_;
        TrayMenuCallback menuCallback_;
        TrayNotificationCallback notificationCallback_;
        
        std::atomic<bool> minimizedToTray_{false};
        std::atomic<bool> notificationsEnabled_{true};
        
        mutable std::mutex mutex_;

#ifdef _WIN32
        void CreateTrayMenu()
        {
            if (hMenu_) {
                DestroyMenu(hMenu_);
            }

            hMenu_ = CreatePopupMenu();
            
            // Add quick actions first
            for (const auto& action : config_.quickActions) {
                std::wstring label(action.label.begin(), action.label.end());
                AppendMenuW(hMenu_, MF_STRING, HashString(action.id), label.c_str());
            }

            if (!config_.quickActions.empty() && !menuItems_.empty()) {
                AppendMenuW(hMenu_, MF_SEPARATOR, 0, nullptr);
            }

            // Add custom menu items
            AddMenuItems(hMenu_, menuItems_);

            // Add separator before standard items
            if (!menuItems_.empty()) {
                AppendMenuW(hMenu_, MF_SEPARATOR, 0, nullptr);
            }

            // Add operations status if any
            if (!operations_.empty() && config_.showOperationProgress) {
                for (const auto& op : operations_) {
                    std::wstring label(op.description.begin(), op.description.end());
                    if (op.progress >= 0) {
                        label += L" (" + std::to_wstring(static_cast<int>(op.progress * 100)) + L"%)";
                    }
                    AppendMenuW(hMenu_, MF_STRING | MF_DISABLED, 0, label.c_str());
                }
                AppendMenuW(hMenu_, MF_SEPARATOR, 0, nullptr);
            }

            // Standard items
            AppendMenuW(hMenu_, MF_STRING, 1001, L"&Show Opacity");
            AppendMenuW(hMenu_, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu_, MF_STRING, 1002, L"E&xit");
        }

        void AddMenuItems(HMENU menu, const std::vector<TrayMenuItem>& items)
        {
            for (const auto& item : items) {
                if (item.separator) {
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                    continue;
                }

                std::wstring label(item.label.begin(), item.label.end());
                UINT flags = MF_STRING;
                
                if (!item.enabled) flags |= MF_DISABLED | MF_GRAYED;
                if (item.checked) flags |= MF_CHECKED;

                if (!item.submenu.empty()) {
                    HMENU submenu = CreatePopupMenu();
                    AddMenuItems(submenu, item.submenu);
                    AppendMenuW(menu, flags | MF_POPUP, 
                        reinterpret_cast<UINT_PTR>(submenu), label.c_str());
                } else {
                    AppendMenuW(menu, flags, HashString(item.id), label.c_str());
                }
            }
        }

        static UINT HashString(const std::string& str)
        {
            // Simple hash for menu item IDs (1000+ range)
            UINT hash = 1000;
            for (char c : str) {
                hash = hash * 31 + static_cast<UINT>(c);
            }
            return (hash % 64000) + 2000;  // Keep in range 2000-66000
        }
#endif
    };

    // ============== SystemTray ==============

    SystemTray::SystemTray()
        : impl_(std::make_unique<Impl>())
    {}

    SystemTray::~SystemTray()
    {
        Shutdown();
    }

    SystemTray::SystemTray(SystemTray&&) noexcept = default;
    SystemTray& SystemTray::operator=(SystemTray&&) noexcept = default;

    bool SystemTray::Initialize(void* hwnd, const SystemTrayConfig& config)
    {
#ifdef _WIN32
        impl_->hwnd_ = static_cast<HWND>(hwnd);
        impl_->config_ = config;

        // Initialize NOTIFYICONDATA
        ZeroMemory(&impl_->nid_, sizeof(impl_->nid_));
        impl_->nid_.cbSize = sizeof(impl_->nid_);
        impl_->nid_.hWnd = impl_->hwnd_;
        impl_->nid_.uID = 1;
        impl_->nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        impl_->nid_.uCallbackMessage = WM_TRAYICON;
        impl_->nid_.uVersion = NOTIFYICON_VERSION_4;

        // Set tooltip
        std::wstring tooltip(config.tooltip.begin(), config.tooltip.end());
        StringCchCopyW(impl_->nid_.szTip, ARRAYSIZE(impl_->nid_.szTip), tooltip.c_str());

        // Load icon
        if (!config.iconPath.empty()) {
            impl_->hIcon_ = static_cast<HICON>(LoadImageW(
                nullptr, 
                std::wstring(config.iconPath.begin(), config.iconPath.end()).c_str(),
                IMAGE_ICON, 
                GetSystemMetrics(SM_CXSMICON), 
                GetSystemMetrics(SM_CYSMICON),
                LR_LOADFROMFILE
            ));
        }

        if (!impl_->hIcon_) {
            // Use default application icon
            impl_->hIcon_ = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
            if (!impl_->hIcon_) {
                impl_->hIcon_ = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION value
            }
        }

        impl_->nid_.hIcon = impl_->hIcon_;

        Logger::Get()->info("SystemTray: Initialized");
        return true;
#else
        return false;
#endif
    }

    void SystemTray::Shutdown()
    {
#ifdef _WIN32
        if (impl_->iconAdded_) {
            Shell_NotifyIconW(NIM_DELETE, &impl_->nid_);
            impl_->iconAdded_ = false;
        }

        if (impl_->hMenu_) {
            DestroyMenu(impl_->hMenu_);
            impl_->hMenu_ = nullptr;
        }

        if (impl_->hIcon_) {
            DestroyIcon(impl_->hIcon_);
            impl_->hIcon_ = nullptr;
        }
#endif
        Logger::Get()->info("SystemTray: Shutdown");
    }

    bool SystemTray::IsInitialized() const
    {
#ifdef _WIN32
        return impl_->hwnd_ != nullptr;
#else
        return false;
#endif
    }

    bool SystemTray::Show()
    {
#ifdef _WIN32
        if (impl_->iconAdded_) {
            return true;
        }

        if (!Shell_NotifyIconW(NIM_ADD, &impl_->nid_)) {
            Logger::Get()->error("SystemTray: Failed to add tray icon");
            return false;
        }

        Shell_NotifyIconW(NIM_SETVERSION, &impl_->nid_);
        impl_->iconAdded_ = true;
        
        Logger::Get()->info("SystemTray: Icon shown");
        return true;
#else
        return false;
#endif
    }

    bool SystemTray::Hide()
    {
#ifdef _WIN32
        if (!impl_->iconAdded_) {
            return true;
        }

        if (!Shell_NotifyIconW(NIM_DELETE, &impl_->nid_)) {
            Logger::Get()->error("SystemTray: Failed to remove tray icon");
            return false;
        }

        impl_->iconAdded_ = false;
        Logger::Get()->info("SystemTray: Icon hidden");
        return true;
#else
        return false;
#endif
    }

    bool SystemTray::IsVisible() const
    {
#ifdef _WIN32
        return impl_->iconAdded_;
#else
        return false;
#endif
    }

    bool SystemTray::SetIcon(const std::string& iconPath)
    {
#ifdef _WIN32
        HICON newIcon = static_cast<HICON>(LoadImageW(
            nullptr,
            std::wstring(iconPath.begin(), iconPath.end()).c_str(),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_LOADFROMFILE
        ));

        if (!newIcon) {
            Logger::Get()->error("SystemTray: Failed to load icon: {}", iconPath);
            return false;
        }

        if (impl_->hIcon_) {
            DestroyIcon(impl_->hIcon_);
        }

        impl_->hIcon_ = newIcon;
        impl_->nid_.hIcon = newIcon;

        if (impl_->iconAdded_) {
            Shell_NotifyIconW(NIM_MODIFY, &impl_->nid_);
        }

        return true;
#else
        return false;
#endif
    }

    bool SystemTray::SetIconFromResource(int resourceId)
    {
#ifdef _WIN32
        HICON newIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resourceId));
        
        if (!newIcon) {
            Logger::Get()->error("SystemTray: Failed to load icon resource: {}", resourceId);
            return false;
        }

        if (impl_->hIcon_) {
            DestroyIcon(impl_->hIcon_);
        }

        impl_->hIcon_ = newIcon;
        impl_->nid_.hIcon = newIcon;

        if (impl_->iconAdded_) {
            Shell_NotifyIconW(NIM_MODIFY, &impl_->nid_);
        }

        return true;
#else
        return false;
#endif
    }

    void SystemTray::SetTooltip(const std::string& tooltip)
    {
#ifdef _WIN32
        std::wstring wtooltip(tooltip.begin(), tooltip.end());
        StringCchCopyW(impl_->nid_.szTip, ARRAYSIZE(impl_->nid_.szTip), wtooltip.c_str());

        if (impl_->iconAdded_) {
            Shell_NotifyIconW(NIM_MODIFY, &impl_->nid_);
        }
#endif
    }

    void SystemTray::FlashIcon(int count, int intervalMs)
    {
#ifdef _WIN32
        // Flash by toggling visibility
        std::thread([this, count, intervalMs]() {
            for (int i = 0; i < count * 2; ++i) {
                if (i % 2 == 0) {
                    Hide();
                } else {
                    Show();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
            }
            Show();  // Ensure visible at end
        }).detach();
#endif
    }

    void SystemTray::SetMenuItems(const std::vector<TrayMenuItem>& items)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->menuItems_ = items;
    }

    void SystemTray::AddQuickAction(const QuickAction& action)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->config_.quickActions.push_back(action);
    }

    void SystemTray::RemoveQuickAction(const std::string& actionId)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        auto& actions = impl_->config_.quickActions;
        actions.erase(
            std::remove_if(actions.begin(), actions.end(),
                [&actionId](const QuickAction& a) { return a.id == actionId; }),
            actions.end()
        );
    }

    void SystemTray::ClearQuickActions()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->config_.quickActions.clear();
    }

    void SystemTray::ShowContextMenu()
    {
#ifdef _WIN32
        POINT pt;
        GetCursorPos(&pt);

        impl_->CreateTrayMenu();

        // Required for menu to work correctly when clicking outside
        SetForegroundWindow(impl_->hwnd_);

        UINT cmd = TrackPopupMenu(
            impl_->hMenu_,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            pt.x, pt.y,
            0,
            impl_->hwnd_,
            nullptr
        );

        // Handle standard commands
        if (cmd == 1001) {  // Show
            RestoreFromTray();
        } else if (cmd == 1002) {  // Exit
            PostMessageW(impl_->hwnd_, WM_CLOSE, 0, 0);
        } else if (cmd >= 2000 && impl_->menuCallback_) {
            // Find matching menu item by hash
            for (const auto& item : impl_->menuItems_) {
                if (Impl::HashString(item.id) == cmd) {
                    impl_->menuCallback_(item.id);
                    break;
                }
            }
            for (const auto& action : impl_->config_.quickActions) {
                if (Impl::HashString(action.id) == cmd) {
                    if (action.action) action.action();
                    break;
                }
            }
        }

        // Fix for menu not closing properly
        PostMessageW(impl_->hwnd_, WM_NULL, 0, 0);
#endif
    }

    bool SystemTray::ShowNotification(const TrayNotification& notification)
    {
#ifdef _WIN32
        if (!impl_->notificationsEnabled_ || !impl_->iconAdded_) {
            return false;
        }

        impl_->nid_.uFlags |= NIF_INFO;
        
        std::wstring title(notification.title.begin(), notification.title.end());
        std::wstring message(notification.message.begin(), notification.message.end());
        
        StringCchCopyW(impl_->nid_.szInfoTitle, ARRAYSIZE(impl_->nid_.szInfoTitle), title.c_str());
        StringCchCopyW(impl_->nid_.szInfo, ARRAYSIZE(impl_->nid_.szInfo), message.c_str());
        
        impl_->nid_.dwInfoFlags = NIIF_NONE;
        switch (notification.urgency) {
            case NotificationUrgency::Low:
                impl_->nid_.dwInfoFlags = NIIF_NONE;
                break;
            case NotificationUrgency::Normal:
                impl_->nid_.dwInfoFlags = NIIF_INFO;
                break;
            case NotificationUrgency::Critical:
                impl_->nid_.dwInfoFlags = NIIF_WARNING;
                break;
        }

        impl_->nid_.uTimeout = notification.timeoutMs;

        bool result = Shell_NotifyIconW(NIM_MODIFY, &impl_->nid_) != FALSE;
        
        impl_->nid_.uFlags &= ~NIF_INFO;
        
        return result;
#else
        return false;
#endif
    }

    bool SystemTray::ShowNotification(const std::string& title, const std::string& message,
                                      NotificationUrgency urgency)
    {
        TrayNotification notification;
        notification.title = title;
        notification.message = message;
        notification.urgency = urgency;
        return ShowNotification(notification);
    }

    void SystemTray::HideNotification()
    {
#ifdef _WIN32
        impl_->nid_.uFlags |= NIF_INFO;
        impl_->nid_.szInfo[0] = L'\0';
        impl_->nid_.szInfoTitle[0] = L'\0';
        Shell_NotifyIconW(NIM_MODIFY, &impl_->nid_);
        impl_->nid_.uFlags &= ~NIF_INFO;
#endif
    }

    void SystemTray::SetNotificationsEnabled(bool enabled)
    {
        impl_->notificationsEnabled_ = enabled;
    }

    bool SystemTray::AreNotificationsEnabled() const
    {
        return impl_->notificationsEnabled_;
    }

    void SystemTray::UpdateOperationStatus(const BackgroundOperationStatus& status)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        auto it = std::find_if(impl_->operations_.begin(), impl_->operations_.end(),
            [&status](const BackgroundOperationStatus& op) { return op.id == status.id; });
        
        if (it != impl_->operations_.end()) {
            *it = status;
        } else {
            impl_->operations_.push_back(status);
        }

        // Update tooltip with operation info
        if (impl_->config_.showOperationProgress && !impl_->operations_.empty()) {
            std::string tooltip = impl_->config_.tooltip;
            tooltip += "\n";
            for (const auto& op : impl_->operations_) {
                tooltip += op.description;
                if (op.progress >= 0) {
                    tooltip += " (" + std::to_string(static_cast<int>(op.progress * 100)) + "%)";
                }
                tooltip += "\n";
            }
            SetTooltip(tooltip);
        }
    }

    void SystemTray::RemoveOperationStatus(const std::string& operationId)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        
        impl_->operations_.erase(
            std::remove_if(impl_->operations_.begin(), impl_->operations_.end(),
                [&operationId](const BackgroundOperationStatus& op) { return op.id == operationId; }),
            impl_->operations_.end()
        );

        if (impl_->operations_.empty()) {
            SetTooltip(impl_->config_.tooltip);
        }
    }

    void SystemTray::ClearOperationStatuses()
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->operations_.clear();
        SetTooltip(impl_->config_.tooltip);
    }

    std::vector<BackgroundOperationStatus> SystemTray::GetActiveOperations() const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        return impl_->operations_;
    }

    void SystemTray::MinimizeToTray()
    {
#ifdef _WIN32
        ShowWindow(impl_->hwnd_, SW_HIDE);
        impl_->minimizedToTray_ = true;
        Show();  // Ensure tray icon is visible
        Logger::Get()->info("SystemTray: Minimized to tray");
#endif
    }

    void SystemTray::RestoreFromTray()
    {
#ifdef _WIN32
        ShowWindow(impl_->hwnd_, SW_SHOW);
        ShowWindow(impl_->hwnd_, SW_RESTORE);
        SetForegroundWindow(impl_->hwnd_);
        impl_->minimizedToTray_ = false;
        
        if (!impl_->config_.showOnMinimize) {
            Hide();
        }
        
        Logger::Get()->info("SystemTray: Restored from tray");
#endif
    }

    bool SystemTray::IsMinimizedToTray() const
    {
        return impl_->minimizedToTray_;
    }

    void SystemTray::SetMinimizeToTray(bool enable)
    {
        impl_->config_.minimizeToTray = enable;
    }

    void SystemTray::SetCloseToTray(bool enable)
    {
        impl_->config_.closeToTray = enable;
    }

    void SystemTray::OnClick(TrayClickCallback callback)
    {
        impl_->clickCallback_ = callback;
    }

    void SystemTray::OnMenuItemSelected(TrayMenuCallback callback)
    {
        impl_->menuCallback_ = callback;
    }

    void SystemTray::OnNotificationClick(TrayNotificationCallback callback)
    {
        impl_->notificationCallback_ = callback;
    }

    const SystemTrayConfig& SystemTray::GetConfig() const
    {
        return impl_->config_;
    }

    void SystemTray::SetConfig(const SystemTrayConfig& config)
    {
        impl_->config_ = config;
    }

    bool SystemTray::ProcessMessage(unsigned int message, uintptr_t wParam, intptr_t lParam)
    {
#ifdef _WIN32
        if (message != WM_TRAYICON) {
            return false;
        }

        UINT event = LOWORD(lParam);
        
        switch (event) {
            case WM_LBUTTONUP:
                if (impl_->clickCallback_) {
                    impl_->clickCallback_(TrayClickType::LeftClick);
                } else {
                    RestoreFromTray();
                }
                return true;

            case WM_RBUTTONUP:
                if (impl_->clickCallback_) {
                    impl_->clickCallback_(TrayClickType::RightClick);
                }
                ShowContextMenu();
                return true;

            case WM_LBUTTONDBLCLK:
                if (impl_->clickCallback_) {
                    impl_->clickCallback_(TrayClickType::DoubleClick);
                } else {
                    RestoreFromTray();
                }
                return true;

            case WM_MBUTTONUP:
                if (impl_->clickCallback_) {
                    impl_->clickCallback_(TrayClickType::MiddleClick);
                }
                return true;

            case NIN_BALLOONUSERCLICK:
                if (impl_->notificationCallback_) {
                    impl_->notificationCallback_();
                }
                return true;
        }
#endif
        return false;
    }

} // namespace opacity::ui
