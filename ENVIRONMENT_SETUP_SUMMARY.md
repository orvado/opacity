# Opacity Build Environment – Setup Summary

## ✅ Completed Setup

Your Opacity project build environment has been fully configured with best practices for C++ development on Windows.

### Project Structure Created

```
c:\Users\Ken\cpp\opacity\
├── CMakeLists.txt                    # Root CMake configuration
├── vcpkg.json                        # Dependency manifest
├── README.md                         # Project overview
├── BUILD_SETUP.md                    # Build instructions
├── ENVIRONMENT_SETUP_SUMMARY.md      # This file
│
├── _plan/
│   ├── PLAN.md                      # Complete feature specification
│   └── phased-development-plan.md   # Implementation roadmap
│
├── build/                           # Build output directory (created by cmake)
├── external/                        # External code (for future use)
├── include/opacity/
│   ├── core/                        # Core subsystem headers
│   │   ├── Logger.h
│   │   ├── Config.h
│   │   └── Path.h
│   ├── filesystem/                  # Filesystem subsystem headers
│   │   ├── FsItem.h
│   │   └── FileSystemManager.h
│   ├── ui/                          # UI subsystem headers
│   │   ├── MainWindow.h
│   │   └── Theme.h
│   ├── search/                      # Search subsystem headers
│   │   ├── SearchEngine.h
│   │   └── FilterEngine.h
│   ├── preview/                     # Preview subsystem headers
│   │   ├── PreviewManager.h
│   │   ├── ImagePreviewHandler.h
│   │   └── TextPreviewHandler.h
│   └── diff/                        # Diff subsystem headers
│       └── DiffEngine.h
│
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp                     # Application entry point
│   ├── core/
│   │   ├── CMakeLists.txt
│   │   ├── Logger.cpp
│   │   ├── Config.cpp
│   │   └── Path.cpp
│   ├── filesystem/
│   │   ├── CMakeLists.txt
│   │   ├── FsItem.cpp
│   │   └── FileSystemManager.cpp
│   ├── ui/
│   │   ├── CMakeLists.txt
│   │   ├── MainWindow.cpp
│   │   └── Theme.cpp
│   ├── search/
│   │   ├── CMakeLists.txt
│   │   ├── SearchEngine.cpp
│   │   └── FilterEngine.cpp
│   ├── preview/
│   │   ├── CMakeLists.txt
│   │   ├── PreviewManager.cpp
│   │   ├── ImagePreviewHandler.cpp
│   │   └── TextPreviewHandler.cpp
│   └── diff/
│       ├── CMakeLists.txt
│       └── DiffEngine.cpp
│
└── tests/
    └── CMakeLists.txt               # Test infrastructure
```

### Configured Subsystems

1. **Core Subsystem** (`include/opacity/core/`)
   - ✅ Logger.h – File-based logging with spdlog
   - ✅ Config.h – JSON configuration management
   - ✅ Path.h – Filesystem abstraction layer

2. **Filesystem Subsystem** (`include/opacity/filesystem/`)
   - ✅ FsItem.h – File/folder model
   - ✅ FileSystemManager.h – File operations (to be expanded)

3. **UI Subsystem** (`include/opacity/ui/`)
   - ✅ MainWindow.h – Application window structure
   - ✅ Theme.h – Light/dark theme management

4. **Search Subsystem** (`include/opacity/search/`)
   - ✅ SearchEngine.h – Query processing
   - ✅ FilterEngine.h – Filtering logic

5. **Preview Subsystem** (`include/opacity/preview/`)
   - ✅ PreviewManager.h – Handler coordination
   - ✅ ImagePreviewHandler.h – Image preview logic
   - ✅ TextPreviewHandler.h – Text preview logic

6. **Diff Subsystem** (`include/opacity/diff/`)
   - ✅ DiffEngine.h – File/folder comparison

---

## 📋 Next Steps to Build

### 1. Install Prerequisites

Ensure you have installed:
- [ ] Visual Studio 2019+ with C++ workload
- [ ] CMake 3.20 or later
- [ ] Git
- [ ] vcpkg

### 2. Set Up vcpkg

```powershell
# Clone vcpkg if not already done
git clone https://github.com/Microsoft/vcpkg.git "C:\Dev\vcpkg"
cd C:\Dev\vcpkg
.\bootstrap-vcpkg.bat

# Add vcpkg to PATH
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\Dev\vcpkg", "User")
$env:VCPKG_ROOT = "C:\Dev\vcpkg"
```

### 3. Install Project Dependencies

```powershell
# Set VCPKG_ROOT if not already set
$env:VCPKG_ROOT = "C:\Dev\vcpkg"

# Install Phase 1 dependencies
cd $env:VCPKG_ROOT
.\vcpkg install `
    nlohmann-json:x64-windows `
    spdlog:x64-windows `
    imgui:x64-windows `
    glfw3:x64-windows `
    stb:x64-windows `
    directx-headers:x64-windows `
    nanosvg:x64-windows
```

### 4. Build the Project

```powershell
cd c:\Users\Ken\cpp\opacity

# Quick build with provided script
.\build.ps1 -Configuration Debug

# Or manual CMake
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows `
    ..
cmake --build . --config Debug --parallel 4
```

### 5. Run the Application

```powershell
cd c:\Users\Ken\cpp\opacity\build
.\bin\Debug\opacity.exe
```

---

## 📚 Documentation Files

### Primary Documentation

1. **README.md** – Project overview and quick start
2. **BUILD_SETUP.md** – Comprehensive build instructions with troubleshooting
3. **_plan/PLAN.md** – Complete feature specification (all phases)
4. **_plan/phased-development-plan.md** – Detailed implementation roadmap

### Development Guidelines

All new code should follow these practices:

- **C++ Standard:** C++17 minimum (C++20 preferred for new code)
- **Memory Management:** Smart pointers (unique_ptr, shared_ptr), RAII
- **Error Handling:** Exceptions for exceptional conditions, logging for diagnostics
- **Code Style:**
  - PascalCase for class names and public functions
  - snake_case for variables and member functions
  - SCREAMING_SNAKE_CASE for constants
- **Comments:** Doxygen-style for public APIs
- **Testing:** Unit tests for core logic, integration tests for subsystems

---

## 🔧 Development Workflow

### During Phase 1 Implementation

1. **Edit source files** in `src/` and `include/opacity/`
2. **Build frequently** with `cmake --build build --config Debug`
3. **Check logs** in `opacity.log` for diagnostic information
4. **Test manually** by running the application
5. **Commit regularly** with clear commit messages

### Running a Development Build

```powershell
# Terminal 1: Build in watch mode (manual rebuild)
cd c:\Users\Ken\cpp\opacity\build
cmake --build . --config Debug

# Terminal 2: Run the application
.\bin\Debug\opacity.exe

# Check generated log file
Get-Content ..\opacity.log -Tail 20  # View last 20 lines
```

### Testing Infrastructure

Phase 1 test infrastructure is ready. When Phase 1 implementation begins:

```powershell
# Run tests
cd c:\Users\Ken\cpp\opacity\build
ctest -C Debug -V

# Build with tests
cmake --build . --config Debug --target RUN_TESTS
```

---

## 📦 Dependency Management

### Current Phase 1 Dependencies

| Package | Purpose | vcpkg Name |
|---------|---------|-----------|
| ImGui | GUI Framework | `imgui` |
| spdlog | Logging | `spdlog` |
| nlohmann/json | Configuration | `nlohmann-json` |
| stb | Image Loading | `stb` |
| GLFW | Window Creation (Optional) | `glfw3` |
| DirectX Headers | Graphics API | `directx-headers` |

### Adding Dependencies for Later Phases

```powershell
# Example: Add FFmpeg for Phase 2 video support
$env:VCPKG_ROOT\vcpkg install ffmpeg:x64-windows

# Update vcpkg.json
# Then rebuild: cmake --build build --clean-first
```

---

## ⚙️ CMake Configuration Details

### Build Targets

The project creates the following targets:

- `opacity` – Main executable
- `opacity_core` – Core library
- `opacity_filesystem` – Filesystem library
- `opacity_ui` – UI library
- `opacity_search` – Search library
- `opacity_preview` – Preview library
- `opacity_diff` – Diff library

### Build Directories

- **Debug build:** `build/bin/Debug/opacity.exe`
- **Release build:** `build/bin/Release/opacity.exe`
- **Libraries:** `build/lib/`
- **CMake cache:** `build/CMakeFiles/`

---

## 🐛 Troubleshooting

### Common Issues

**"CMake not found"**
```powershell
# Add to PATH
$env:PATH += ";C:\Program Files\CMake\bin"
```

**"vcpkg: Command not found"**
```powershell
$env:VCPKG_ROOT = "C:\Dev\vcpkg"
$env:PATH += ";$env:VCPKG_ROOT"
```

**"Visual Studio not found"**
```powershell
# Check available VS installations
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -all
```

**Build linking errors**
```powershell
# Ensure all dependencies installed
$env:VCPKG_ROOT\vcpkg list

# Rebuild CMake cache
rm -r build\CMakeFiles
cmake .. -G "Visual Studio 17 2022"
```

See **BUILD_SETUP.md** for more troubleshooting help.

---

## 🚀 Ready for Phase 1 Development

Your build environment is now ready. To begin Phase 1 implementation:

1. ✅ Build environment configured
2. ✅ Project structure in place
3. ✅ Core headers and stubs created
4. ✅ CMake configuration complete
5. ✅ Dependencies listed (install before build)

**Next:** Follow the Phase 1 implementation tasks in `_plan/phased-development-plan.md`

### Quick Commands Reference

```powershell
# Build
.\build.ps1 -Configuration Debug

# Open in Visual Studio
start .\build\Opacity.sln

# Run debug executable
.\build\bin\Debug\opacity.exe

# Clean build
.\build.ps1 -Configuration Debug -Clean -OpenSolution

# View build log
Get-Content .\build\CMakeOutput.log

# List installed dependencies
$env:VCPKG_ROOT\vcpkg list --installed
```

---

## 📞 Getting Help

- **Build Issues:** See BUILD_SETUP.md
- **Architecture Questions:** See _plan/PLAN.md
- **Phase 1 Tasks:** See _plan/phased-development-plan.md
- **API Documentation:** Headers in include/opacity/

---

**Environment setup complete. Ready to begin Phase 1 development! 🎉**
