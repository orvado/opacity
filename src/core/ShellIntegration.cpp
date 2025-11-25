#include "opacity/core/ShellIntegration.h"
#include "opacity/core/Logger.h"

#include <algorithm>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sddl.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace opacity::core
{
    // ============== CommandLineResult Implementation ==============

    bool CommandLineResult::HasFlag(const std::string& flag) const
    {
        return std::find(flags.begin(), flags.end(), flag) != flags.end();
    }

    std::optional<std::string> CommandLineResult::GetOption(const std::string& key) const
    {
        auto it = options.find(key);
        if (it != options.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::filesystem::path CommandLineResult::GetFirstPath() const
    {
        if (!paths.empty()) {
            return paths.front();
        }
        return {};
    }

    // ============== IpcMessage Implementation ==============

    std::string IpcMessage::Serialize() const
    {
        std::ostringstream oss;
        oss << static_cast<int>(type) << "|";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) oss << "\x1F";  // Unit separator
            oss << args[i];
        }
        return oss.str();
    }

    std::optional<IpcMessage> IpcMessage::Deserialize(const std::string& data)
    {
        auto pos = data.find('|');
        if (pos == std::string::npos) return std::nullopt;

        try {
            IpcMessage msg;
            msg.type = static_cast<IpcMessageType>(std::stoi(data.substr(0, pos)));
            
            std::string argsStr = data.substr(pos + 1);
            if (!argsStr.empty()) {
                std::istringstream iss(argsStr);
                std::string arg;
                while (std::getline(iss, arg, '\x1F')) {
                    msg.args.push_back(arg);
                }
            }
            return msg;
        }
        catch (...) {
            return std::nullopt;
        }
    }

    // ============== ShellIntegration::Impl ==============

    class ShellIntegration::Impl
    {
    public:
        Impl() = default;
        ~Impl() { Shutdown(); }

#ifdef _WIN32
        HANDLE mutex_ = nullptr;
        HANDLE pipeServer_ = INVALID_HANDLE_VALUE;
        std::thread listenerThread_;
        std::atomic<bool> listenerRunning_{false};
        std::mutex listenerMutex_;
        IpcMessageHandler messageHandler_;
        std::string pipeName_;
        std::string mutexName_;

        void ListenerThreadFunc()
        {
            const std::string fullPipeName = "\\\\.\\pipe\\" + pipeName_;
            
            while (listenerRunning_) {
                // Create named pipe server
                pipeServer_ = CreateNamedPipeA(
                    fullPipeName.c_str(),
                    PIPE_ACCESS_INBOUND,
                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                    1,  // Max instances
                    1024,
                    1024,
                    0,
                    nullptr
                );

                if (pipeServer_ == INVALID_HANDLE_VALUE) {
                    Logger::Get()->error("ShellIntegration: Failed to create pipe server");
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }

                // Wait for client connection
                if (ConnectNamedPipe(pipeServer_, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
                    // Read message
                    char buffer[4096];
                    DWORD bytesRead = 0;
                    
                    if (ReadFile(pipeServer_, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
                        buffer[bytesRead] = '\0';
                        
                        auto message = IpcMessage::Deserialize(std::string(buffer));
                        if (message && messageHandler_) {
                            std::lock_guard<std::mutex> lock(listenerMutex_);
                            messageHandler_(*message);
                        }
                    }
                }

                DisconnectNamedPipe(pipeServer_);
                CloseHandle(pipeServer_);
                pipeServer_ = INVALID_HANDLE_VALUE;
            }
        }
#endif

        void Shutdown()
        {
#ifdef _WIN32
            listenerRunning_ = false;
            
            if (pipeServer_ != INVALID_HANDLE_VALUE) {
                CloseHandle(pipeServer_);
                pipeServer_ = INVALID_HANDLE_VALUE;
            }
            
            if (listenerThread_.joinable()) {
                // Send dummy message to unblock pipe wait
                const std::string fullPipeName = "\\\\.\\pipe\\" + pipeName_;
                HANDLE client = CreateFileA(
                    fullPipeName.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    OPEN_EXISTING,
                    0,
                    nullptr
                );
                if (client != INVALID_HANDLE_VALUE) {
                    CloseHandle(client);
                }
                listenerThread_.join();
            }
            
            if (mutex_) {
                ReleaseMutex(mutex_);
                CloseHandle(mutex_);
                mutex_ = nullptr;
            }
#endif
        }
    };

    // ============== ShellIntegration ==============

    ShellIntegration::ShellIntegration()
        : impl_(std::make_unique<Impl>())
    {}

    ShellIntegration::~ShellIntegration()
    {
        Shutdown();
    }

    ShellIntegration::ShellIntegration(ShellIntegration&&) noexcept = default;
    ShellIntegration& ShellIntegration::operator=(ShellIntegration&&) noexcept = default;

    bool ShellIntegration::Initialize(const ShellIntegrationConfig& config)
    {
        config_ = config;
        
#ifdef _WIN32
        impl_->mutexName_ = config_.mutexName;
        impl_->pipeName_ = config_.pipeName;
#endif
        
        initialized_ = true;
        Logger::Get()->info("ShellIntegration: Initialized");
        return true;
    }

    void ShellIntegration::Shutdown()
    {
        if (impl_) {
            impl_->Shutdown();
        }
        initialized_ = false;
    }

    // ============== Context Menu Registration ==============

    bool ShellIntegration::RegisterContextMenu()
    {
#ifdef _WIN32
        if (!IsRunningAsAdmin()) {
            Logger::Get()->warn("ShellIntegration: Admin rights required for context menu registration");
            return false;
        }

        const auto exePath = GetExecutablePath();
        const std::string exePathStr = exePath.string();

        // Register for folders
        HKEY hKey;
        std::string keyPath = "Directory\\Background\\shell\\OpacityOpen";
        
        if (RegCreateKeyExA(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            std::string menuText = "Open in Opacity";
            RegSetValueExA(hKey, nullptr, 0, REG_SZ, 
                reinterpret_cast<const BYTE*>(menuText.c_str()), 
                static_cast<DWORD>(menuText.length() + 1));
            
            // Set icon
            RegSetValueExA(hKey, "Icon", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(exePathStr.c_str()),
                static_cast<DWORD>(exePathStr.length() + 1));
            
            RegCloseKey(hKey);

            // Create command subkey
            keyPath += "\\command";
            if (RegCreateKeyExA(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
            {
                std::string command = "\"" + exePathStr + "\" \"%V\"";
                RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(command.c_str()),
                    static_cast<DWORD>(command.length() + 1));
                RegCloseKey(hKey);
            }
        }

        // Register for folder items
        keyPath = "Directory\\shell\\OpacityOpen";
        if (RegCreateKeyExA(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            std::string menuText = "Open in Opacity";
            RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(menuText.c_str()),
                static_cast<DWORD>(menuText.length() + 1));
            RegSetValueExA(hKey, "Icon", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(exePathStr.c_str()),
                static_cast<DWORD>(exePathStr.length() + 1));
            RegCloseKey(hKey);

            keyPath += "\\command";
            if (RegCreateKeyExA(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
            {
                std::string command = "\"" + exePathStr + "\" \"%1\"";
                RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(command.c_str()),
                    static_cast<DWORD>(command.length() + 1));
                RegCloseKey(hKey);
            }
        }

        // Notify shell of changes
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        
        Logger::Get()->info("ShellIntegration: Context menu registered");
        return true;
#else
        return false;
#endif
    }

    bool ShellIntegration::UnregisterContextMenu()
    {
#ifdef _WIN32
        if (!IsRunningAsAdmin()) {
            Logger::Get()->warn("ShellIntegration: Admin rights required for context menu unregistration");
            return false;
        }

        // Delete registry keys
        RegDeleteTreeA(HKEY_CLASSES_ROOT, "Directory\\Background\\shell\\OpacityOpen");
        RegDeleteTreeA(HKEY_CLASSES_ROOT, "Directory\\shell\\OpacityOpen");
        
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        
        Logger::Get()->info("ShellIntegration: Context menu unregistered");
        return true;
#else
        return false;
#endif
    }

    bool ShellIntegration::IsContextMenuRegistered() const
    {
#ifdef _WIN32
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CLASSES_ROOT, "Directory\\shell\\OpacityOpen", 
            0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return true;
        }
        return false;
#else
        return false;
#endif
    }

    // ============== File Associations ==============

    bool ShellIntegration::RegisterFileAssociation(const std::string& extension)
    {
#ifdef _WIN32
        if (!IsRunningAsAdmin()) {
            return false;
        }

        const auto exePath = GetExecutablePath();
        const std::string exePathStr = exePath.string();

        // Create ProgID
        std::string progId = "Opacity." + extension.substr(1);  // Remove leading dot
        HKEY hKey;
        
        if (RegCreateKeyExA(HKEY_CLASSES_ROOT, progId.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            std::string desc = "Opacity File";
            RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(desc.c_str()),
                static_cast<DWORD>(desc.length() + 1));
            RegCloseKey(hKey);

            std::string cmdKeyPath = progId + "\\shell\\open\\command";
            if (RegCreateKeyExA(HKEY_CLASSES_ROOT, cmdKeyPath.c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
            {
                std::string command = "\"" + exePathStr + "\" \"%1\"";
                RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(command.c_str()),
                    static_cast<DWORD>(command.length() + 1));
                RegCloseKey(hKey);
            }
        }

        // Associate extension with ProgID
        if (RegCreateKeyExA(HKEY_CLASSES_ROOT, extension.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(progId.c_str()),
                static_cast<DWORD>(progId.length() + 1));
            RegCloseKey(hKey);
        }

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return true;
#else
        return false;
#endif
    }

    bool ShellIntegration::UnregisterFileAssociation(const std::string& extension)
    {
#ifdef _WIN32
        if (!IsRunningAsAdmin()) {
            return false;
        }

        std::string progId = "Opacity." + extension.substr(1);
        RegDeleteTreeA(HKEY_CLASSES_ROOT, progId.c_str());
        
        // Note: We don't delete the extension key as other apps may use it
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CLASSES_ROOT, extension.c_str(),
            0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS)
        {
            // Only clear if it points to our ProgID
            char buffer[256];
            DWORD size = sizeof(buffer);
            if (RegQueryValueExA(hKey, nullptr, nullptr, nullptr, 
                reinterpret_cast<BYTE*>(buffer), &size) == ERROR_SUCCESS)
            {
                if (std::string(buffer) == progId) {
                    RegDeleteValueA(hKey, nullptr);
                }
            }
            RegCloseKey(hKey);
        }

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return true;
#else
        return false;
#endif
    }

    bool ShellIntegration::RegisterAsDefaultBrowser()
    {
        // This is a complex operation requiring user consent on modern Windows
        // For now, return false to indicate not implemented
        Logger::Get()->warn("ShellIntegration: RegisterAsDefaultBrowser not implemented");
        return false;
    }

    bool ShellIntegration::UnregisterAsDefaultBrowser()
    {
        return false;
    }

    // ============== Send To Menu ==============

    bool ShellIntegration::AddToSendToMenu()
    {
#ifdef _WIN32
        // Get Send To folder path
        wchar_t sendToPath[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_SENDTO, nullptr, 0, sendToPath))) {
            Logger::Get()->error("ShellIntegration: Failed to get Send To folder path");
            return false;
        }

        const auto exePath = GetExecutablePath();
        std::filesystem::path linkPath = std::filesystem::path(sendToPath) / "Opacity.lnk";

        // Create shortcut using COM
        CoInitialize(nullptr);
        
        IShellLinkW* psl = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
            IID_IShellLinkW, reinterpret_cast<void**>(&psl));
        
        if (SUCCEEDED(hr)) {
            psl->SetPath(exePath.wstring().c_str());
            psl->SetDescription(L"Open in Opacity File Explorer");
            psl->SetIconLocation(exePath.wstring().c_str(), 0);

            IPersistFile* ppf = nullptr;
            hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
            if (SUCCEEDED(hr)) {
                hr = ppf->Save(linkPath.wstring().c_str(), TRUE);
                ppf->Release();
            }
            psl->Release();
        }
        
        CoUninitialize();

        if (SUCCEEDED(hr)) {
            Logger::Get()->info("ShellIntegration: Added to Send To menu");
            return true;
        }
        return false;
#else
        return false;
#endif
    }

    bool ShellIntegration::RemoveFromSendToMenu()
    {
#ifdef _WIN32
        wchar_t sendToPath[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_SENDTO, nullptr, 0, sendToPath))) {
            return false;
        }

        std::filesystem::path linkPath = std::filesystem::path(sendToPath) / "Opacity.lnk";
        
        std::error_code ec;
        if (std::filesystem::exists(linkPath, ec)) {
            std::filesystem::remove(linkPath, ec);
            if (!ec) {
                Logger::Get()->info("ShellIntegration: Removed from Send To menu");
                return true;
            }
        }
        return false;
#else
        return false;
#endif
    }

    bool ShellIntegration::IsInSendToMenu() const
    {
#ifdef _WIN32
        wchar_t sendToPath[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_SENDTO, nullptr, 0, sendToPath))) {
            return false;
        }

        std::filesystem::path linkPath = std::filesystem::path(sendToPath) / "Opacity.lnk";
        std::error_code ec;
        return std::filesystem::exists(linkPath, ec);
#else
        return false;
#endif
    }

    // ============== Command Line ==============

    CommandLineResult ShellIntegration::ParseCommandLine(int argc, char* argv[])
    {
        CommandLineResult result;
        result.valid = true;

        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            
            if (arg.empty()) continue;

            // Check for flags/options
            if (arg[0] == '-') {
                // Long option
                if (arg.length() > 1 && arg[1] == '-') {
                    std::string optName = arg.substr(2);
                    
                    // Check for = sign
                    auto eqPos = optName.find('=');
                    if (eqPos != std::string::npos) {
                        result.options[optName.substr(0, eqPos)] = optName.substr(eqPos + 1);
                    } else {
                        result.flags.push_back(optName);
                    }
                }
                // Short option
                else {
                    for (size_t j = 1; j < arg.length(); ++j) {
                        result.flags.push_back(std::string(1, arg[j]));
                    }
                }
            }
            // Check if it's a command
            else if (result.command.empty() && 
                     (arg == "open" || arg == "search" || arg == "compare" || 
                      arg == "help" || arg == "version" || arg == "register" ||
                      arg == "unregister"))
            {
                result.command = arg;
            }
            // Treat as path
            else {
                std::filesystem::path path(arg);
                std::error_code ec;
                
                // Expand to absolute path if relative
                if (path.is_relative()) {
                    path = std::filesystem::absolute(path, ec);
                }
                
                result.paths.push_back(path);
            }
        }

        // Default command is "open" if we have paths
        if (result.command.empty() && !result.paths.empty()) {
            result.command = "open";
        }

        return result;
    }

    CommandLineResult ShellIntegration::ParseCommandLine(const std::string& cmdLine)
    {
        // Simple tokenizer (doesn't handle quoted strings perfectly)
        std::vector<std::string> args;
        std::istringstream iss(cmdLine);
        std::string token;
        
        bool inQuotes = false;
        std::string current;
        
        for (char c : cmdLine) {
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ' ' && !inQuotes) {
                if (!current.empty()) {
                    args.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            args.push_back(current);
        }

        // Convert to argc/argv style
        std::vector<char*> argv;
        for (auto& arg : args) {
            argv.push_back(&arg[0]);
        }

        return ParseCommandLine(static_cast<int>(argv.size()), argv.data());
    }

    CommandLineResult ShellIntegration::ParseWindowsCommandLine()
    {
#ifdef _WIN32
        int argc;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        
        if (!argv) {
            CommandLineResult result;
            result.valid = false;
            result.error = "Failed to parse command line";
            return result;
        }

        // Convert wide strings to narrow
        std::vector<std::string> argsNarrow;
        for (int i = 0; i < argc; ++i) {
            int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
            std::string narrow(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &narrow[0], len, nullptr, nullptr);
            argsNarrow.push_back(std::move(narrow));
        }

        LocalFree(argv);

        // Convert to char** format
        std::vector<char*> argvNarrow;
        for (auto& arg : argsNarrow) {
            argvNarrow.push_back(&arg[0]);
        }

        return ParseCommandLine(static_cast<int>(argvNarrow.size()), argvNarrow.data());
#else
        CommandLineResult result;
        result.valid = false;
        result.error = "Not implemented for this platform";
        return result;
#endif
    }

    std::string ShellIntegration::GetUsageText() const
    {
        return R"(Opacity File Explorer

Usage: opacity [command] [options] [paths...]

Commands:
  open        Open a file or folder (default if paths provided)
  search      Search for files
  compare     Compare two folders
  register    Register shell integration (requires admin)
  unregister  Remove shell integration (requires admin)
  help        Show this help message
  version     Show version information

Options:
  --new-window    Open in a new window
  --left-panel    Open path in left panel
  --right-panel   Open path in right panel
  --search=TEXT   Search for text
  --filter=EXPR   Apply filter expression

Examples:
  opacity C:\Users\Me\Documents
  opacity compare "C:\Folder1" "C:\Folder2"
  opacity search --filter="*.cpp" "C:\Project"
  opacity register
)";
    }

    std::string ShellIntegration::GetVersionText() const
    {
        return "Opacity File Explorer v0.3.0 (Phase 3)";
    }

    // ============== Single Instance ==============

    bool ShellIntegration::IsPrimaryInstance()
    {
#ifdef _WIN32
        if (impl_->mutex_) {
            return true;  // Already checked
        }

        impl_->mutex_ = CreateMutexA(nullptr, TRUE, impl_->mutexName_.c_str());
        DWORD error = GetLastError();

        if (error == ERROR_ALREADY_EXISTS) {
            if (impl_->mutex_) {
                CloseHandle(impl_->mutex_);
                impl_->mutex_ = nullptr;
            }
            return false;
        }

        return impl_->mutex_ != nullptr;
#else
        return true;
#endif
    }

    bool ShellIntegration::SendToPrimaryInstance(const IpcMessage& message)
    {
#ifdef _WIN32
        const std::string fullPipeName = "\\\\.\\pipe\\" + impl_->pipeName_;
        
        HANDLE pipe = CreateFileA(
            fullPipeName.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (pipe == INVALID_HANDLE_VALUE) {
            Logger::Get()->error("ShellIntegration: Failed to connect to primary instance");
            return false;
        }

        std::string data = message.Serialize();
        DWORD bytesWritten = 0;
        bool success = WriteFile(pipe, data.c_str(), 
            static_cast<DWORD>(data.length()), &bytesWritten, nullptr) != 0;
        
        CloseHandle(pipe);
        return success;
#else
        return false;
#endif
    }

    bool ShellIntegration::StartIpcListener(IpcMessageHandler handler)
    {
#ifdef _WIN32
        if (impl_->listenerRunning_) {
            return true;  // Already running
        }

        impl_->messageHandler_ = handler;
        impl_->listenerRunning_ = true;
        impl_->listenerThread_ = std::thread(&Impl::ListenerThreadFunc, impl_.get());
        
        Logger::Get()->info("ShellIntegration: IPC listener started");
        return true;
#else
        return false;
#endif
    }

    void ShellIntegration::StopIpcListener()
    {
        impl_->Shutdown();
    }

    bool ShellIntegration::IsIpcListenerRunning() const
    {
        return impl_->listenerRunning_;
    }

    // ============== Admin Rights ==============

    bool ShellIntegration::IsRunningAsAdmin()
    {
#ifdef _WIN32
        BOOL isAdmin = FALSE;
        PSID adminGroup = nullptr;
        
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
        if (AllocateAndInitializeSid(&ntAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &adminGroup))
        {
            CheckTokenMembership(nullptr, adminGroup, &isAdmin);
            FreeSid(adminGroup);
        }
        
        return isAdmin != FALSE;
#else
        return getuid() == 0;
#endif
    }

    bool ShellIntegration::RequestElevation(const std::string& parameters)
    {
#ifdef _WIN32
        const auto exePath = GetExecutablePath();
        
        SHELLEXECUTEINFOA sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = "runas";
        sei.lpFile = exePath.string().c_str();
        sei.lpParameters = parameters.c_str();
        sei.nShow = SW_NORMAL;

        if (ShellExecuteExA(&sei)) {
            // Wait for elevated process to finish (optional)
            if (sei.hProcess) {
                WaitForSingleObject(sei.hProcess, INFINITE);
                CloseHandle(sei.hProcess);
            }
            return true;
        }
        
        return false;
#else
        return false;
#endif
    }

    std::filesystem::path ShellIntegration::GetExecutablePath()
    {
#ifdef _WIN32
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::filesystem::path(path);
#else
        char path[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len != -1) {
            path[len] = '\0';
            return std::filesystem::path(path);
        }
        return {};
#endif
    }

    void ShellIntegration::SetConfig(const ShellIntegrationConfig& config)
    {
        config_ = config;
#ifdef _WIN32
        impl_->mutexName_ = config_.mutexName;
        impl_->pipeName_ = config_.pipeName;
#endif
    }

    // ============== ShellIntegrationBuilder ==============

    ShellIntegrationBuilder& ShellIntegrationBuilder::WithContextMenu(bool enable)
    {
        config_.enableContextMenu = enable;
        return *this;
    }

    ShellIntegrationBuilder& ShellIntegrationBuilder::WithSendTo(bool enable)
    {
        config_.addToSendTo = enable;
        return *this;
    }

    ShellIntegrationBuilder& ShellIntegrationBuilder::WithSingleInstance(bool enable)
    {
        config_.enforceSingleInstance = enable;
        return *this;
    }

    ShellIntegrationBuilder& ShellIntegrationBuilder::WithMutexName(const std::string& name)
    {
        config_.mutexName = name;
        return *this;
    }

    ShellIntegrationBuilder& ShellIntegrationBuilder::WithPipeName(const std::string& name)
    {
        config_.pipeName = name;
        return *this;
    }

    ShellIntegrationBuilder& ShellIntegrationBuilder::WithFileAssociations(const std::vector<std::string>& exts)
    {
        config_.associatedExtensions = exts;
        return *this;
    }

    ShellIntegrationConfig ShellIntegrationBuilder::Build() const
    {
        return config_;
    }

    std::unique_ptr<ShellIntegration> ShellIntegrationBuilder::Create() const
    {
        auto shell = std::make_unique<ShellIntegration>();
        shell->Initialize(config_);
        return shell;
    }

} // namespace opacity::core

