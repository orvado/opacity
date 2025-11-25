#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace opacity::ui
{
    /**
     * @brief ImGui backend for Win32 + DirectX 11
     * 
     * Manages the DirectX 11 device, swap chain, and ImGui context.
     * Provides the rendering foundation for the application.
     */
    class ImGuiBackend
    {
    public:
        ImGuiBackend() = default;
        ~ImGuiBackend();

        // Prevent copying
        ImGuiBackend(const ImGuiBackend&) = delete;
        ImGuiBackend& operator=(const ImGuiBackend&) = delete;

        /**
         * @brief Initialize Win32 window and DirectX 11 device
         * @param title Window title
         * @param width Initial window width
         * @param height Initial window height
         * @return true if initialization succeeded
         */
        bool Initialize(const std::wstring& title, int width, int height);

        /**
         * @brief Shutdown and cleanup all resources
         */
        void Shutdown();

        /**
         * @brief Begin a new frame for ImGui rendering
         * @return true if frame can be rendered, false if window is minimized
         */
        bool BeginFrame();

        /**
         * @brief End frame and present to screen
         */
        void EndFrame();

        /**
         * @brief Process Win32 messages
         * @return true if application should continue, false if quit requested
         */
        bool ProcessMessages();

        /**
         * @brief Check if window is still valid
         */
        bool IsRunning() const { return running_ && hwnd_ != nullptr; }

        /**
         * @brief Get the window handle
         */
        HWND GetHwnd() const { return hwnd_; }

        /**
         * @brief Get window dimensions
         */
        int GetWidth() const { return width_; }
        int GetHeight() const { return height_; }

        /**
         * @brief Get the D3D11 device for texture creation
         */
        ID3D11Device* GetDevice() const { return device_.Get(); }

    private:
        bool CreateDeviceD3D();
        void CleanupDeviceD3D();
        void CreateRenderTarget();
        void CleanupRenderTarget();
        void HandleResize(int width, int height);

        static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // Win32
        HWND hwnd_ = nullptr;
        WNDCLASSEXW wc_ = {};
        int width_ = 1280;
        int height_ = 720;
        bool running_ = false;

        // DirectX 11
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> device_context_;
        Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> main_render_target_view_;

        // Clear color (dark theme)
        float clear_color_[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    };

} // namespace opacity::ui
