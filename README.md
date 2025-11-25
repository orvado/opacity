# Opacity – Windows File Manager Replacement

A modern, high-performance file manager for Windows, built with C++ and Dear ImGui, designed as a complete replacement for Windows File Explorer with advanced features including dual-pane browsing, powerful search and filtering, rich media previews, and integrated file/folder comparison tools.

## Project Status

**Current Phase:** Development Environment Setup Complete  
**Target Platform:** Windows 10+  
**Language:** C++17/20  
**GUI Framework:** Dear ImGui with DirectX 11

## Key Features (Complete Roadmap)

- ✅ **Modular Architecture** – Cleanly separated subsystems (Core, Filesystem, UI, Search, Preview, Diff)
- ⬜ **Dual-Pane Interface** – Independent navigation with synchronized operations (Phase 2)
- ⬜ **Tabbed Browsing** – Multiple tabs per pane with history (Phase 1)
- ⬜ **Advanced Search** – Name, content, size, date, attribute-based search (Phase 2)
- ⬜ **Rich Previews** – Images, video, audio, text with syntax highlighting (Phases 1-2)
- ⬜ **File/Folder Diff** – Compare files and folders visually (Phase 2-3)
- ⬜ **Batch Operations** – Rename, copy, move, delete with progress (Phase 1-2)
- ⬜ **Customization** – Themes, hotkeys, layouts, presets (Phases 1-3)

## Architecture

```
Opacity/
├── Core Subsystem
│   ├── Logger (file-based logging)
│   ├── Config (JSON-based settings)
│   └── Path (filesystem abstraction)
├── Filesystem Subsystem
│   ├── FsItem (file/folder model)
│   └── FileSystemManager (operations)
├── UI Subsystem
│   ├── MainWindow (application window)
│   ├── Theme (light/dark modes)
│   └── (Panes, Tabs, Dialogs – Phase 1+)
├── Search Subsystem
│   ├── SearchEngine (query processing)
│   └── FilterEngine (filtering logic)
├── Preview Subsystem
│   ├── PreviewManager (handler coordination)
│   ├── ImagePreviewHandler
│   ├── TextPreviewHandler
│   └── (Media handlers – Phase 2+)
└── Diff Subsystem
    └── DiffEngine (comparison logic)
```

## Build Environment

### Quick Start

```powershell
# 1. Install dependencies via vcpkg
cd C:\Dev\vcpkg
.\vcpkg install nlohmann-json:x64-windows spdlog:x64-windows imgui:x64-windows glfw3:x64-windows stb:x64-windows directx-headers:x64-windows

# 2. Configure and build
cd c:\Users\Ken\cpp\opacity
.\build.ps1 -Configuration Debug

# 3. Run
.\build\bin\Debug\opacity.exe
```

For detailed setup instructions, see [BUILD_SETUP.md](BUILD_SETUP.md).

## Development Phases

### Phase 1: Foundation & Basic Explorer (4-6 weeks)
- Basic file browser with navigation
- Single-pane tabbed interface
- Core file operations (copy, move, delete, rename)
- Basic search and filtering
- Image and text previews
- Settings system

### Phase 2: Enhanced Interface & Advanced Operations (5-7 weeks)
- Dual-pane layout
- Advanced search with multiple criteria
- Video/audio previews
- Batch rename with preview
- File and folder diff capabilities
- Color tagging

### Phase 3: Power Features & Integration (6-8 weeks)
- Folder comparison and sync
- Archive management
- System integration (shell, CLI)
- Real-time monitoring
- Advanced customization

### Phase 4: Polish & Extensibility (4-6 weeks)
- Plugin architecture
- Advanced media support
- Network/cloud features
- Comprehensive testing
- Documentation

See [_plan/phased-development-plan.md](_plan/phased-development-plan.md) for detailed phase requirements.

## Documentation

- **[PLAN.md](_plan/PLAN.md)** – Complete feature specification
- **[phased-development-plan.md](_plan/phased-development-plan.md)** – Implementation roadmap
- **[BUILD_SETUP.md](BUILD_SETUP.md)** – Build environment setup guide
- **[Contributing Guidelines](#contributing)** – Development standards

## Technology Stack

### Core Dependencies (vcpkg)
- **ImGui** – Immediate mode GUI framework
- **nlohmann/json** – JSON configuration
- **spdlog** – Structured logging
- **stb** – Image loading (Phase 1)
- **DirectX Headers** – Graphics API

### Phase 2+ Libraries
- **FFmpeg** – Video/audio playback
- **libzip** – Archive handling
- **MuPDF** – PDF rendering

## Building

### Prerequisites
- Visual Studio 2019+ with C++ workload
- CMake 3.20+
- vcpkg package manager

### Build Steps

```powershell
# Configure
cmake -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -B build

# Compile
cmake --build build --config Debug --parallel 4

# Run
.\build\bin\Debug\opacity.exe
```

See [BUILD_SETUP.md](BUILD_SETUP.md) for detailed instructions.

## No REST API – GUI-First Design

**Important:** Opacity is designed as a **standalone GUI application** with no REST API services. All functionality is:
- **Directly accessible** through the user interface
- **Keyboard-driven** with comprehensive shortcut support
- **Mouse-friendly** with intuitive interactions
- **Self-contained** in a single executable (plus optional plugins)

All file operations, searches, comparisons, and previews are handled locally through GUI interactions.

## Design Principles

1. **Incremental Delivery** – Each phase produces a working, usable application
2. **Modern C++** – C++17/20 with RAII, smart pointers, and best practices
3. **Performance-First** – Virtual scrolling, async operations, lazy loading
4. **User-Centric** – Keyboard shortcuts, customization, accessibility
5. **Maintainability** – Clear separation of concerns, modular design

## Performance Targets

- **Startup:** < 1 second on SSD
- **Directory Listing:** 10,000+ files without lag
- **Memory:** < 150 MB baseline
- **UI Rendering:** 60 FPS
- **Operations:** Progress shown for tasks > 300ms

## Contributing

This project follows modern C++ best practices:
- C++17 standard minimum, C++20 encouraged
- RAII and smart pointers for memory management
- Clear error handling and logging
- Comprehensive code comments
- Unit and integration tests for new features

## License

To be determined (likely MIT or similar open-source)

## Roadmap

| Timeline | Milestone |
|----------|-----------|
| Weeks 1-4 | Phase 1: Core explorer MVP |
| Weeks 5-9 | Phase 2: Advanced operations |
| Weeks 10-15 | Phase 3: Power features & integration |
| Weeks 16-19 | Phase 4: Polish & extensibility |
| Week 20+ | Release candidate & optimization |

## Support & Feedback

For detailed implementation progress and phase-specific tasks, see:
- [_plan/phased-development-plan.md](_plan/phased-development-plan.md) – Detailed phase requirements
- [BUILD_SETUP.md](BUILD_SETUP.md) – Development environment setup
- GitHub Issues – Feature requests and bug reports

## Quick Commands

```powershell
# Full clean build
.\build.ps1 -Configuration Release -Clean

# Debug build with solution opened
.\build.ps1 -Configuration Debug -OpenSolution

# Run tests
cd build
ctest -C Debug -V
```

---

**Opacity** – The modern Windows file manager. Fast. Powerful. Yours.
