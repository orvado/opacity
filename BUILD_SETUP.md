# Opacity Build Environment Setup & Build Instructions

## Prerequisites

### System Requirements
- Windows 10 or later (x64)
- Visual Studio 2019 or later (with C++ workload)
- CMake 3.20 or later
- Git for version control

### Required Software Installation

#### 1. Install Visual Studio (if not already installed)
```powershell
# Download from: https://visualstudio.microsoft.com/downloads/
# Or use Visual Studio Installer to modify existing installation
# Ensure "Desktop development with C++" workload is installed
```

#### 2. Install CMake
```powershell
# Option 1: Using chocolatey (if installed)
choco install cmake

# Option 2: Manual download
# Download from: https://cmake.org/download/
# Add CMake to PATH (usually installed in C:\Program Files\CMake\bin)
```

#### 3. Install vcpkg (Package Manager)
```powershell
# Clone vcpkg repository
cd "C:\Dev"  # or your preferred location
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# Run bootstrap script
.\bootstrap-vcpkg.bat

# Add vcpkg to PATH or user environment variables
# Set VCPKG_ROOT=C:\Dev\vcpkg
# Add C:\Dev\vcpkg to PATH
```

---

## Project Structure

```
opacity/
├── CMakeLists.txt                 # Root CMake configuration
├── vcpkg.json                     # vcpkg dependencies
├── _plan/                         # Development planning docs
│   ├── PLAN.md                   # Full specification
│   └── phased-development-plan.md # Implementation phases
├── build/                         # Build output directory
├── external/                      # External libraries (if needed)
├── include/opacity/               # Public header files
│   ├── core/                     # Core subsystem
│   ├── filesystem/               # Filesystem subsystem
│   ├── ui/                       # UI subsystem
│   ├── search/                   # Search subsystem
│   ├── preview/                  # Preview subsystem
│   └── diff/                     # Diff subsystem
├── src/                          # Implementation files
│   ├── CMakeLists.txt           # Main executable CMake config
│   ├── main.cpp                 # Entry point
│   ├── core/                    # Core subsystem implementation
│   ├── filesystem/              # Filesystem subsystem implementation
│   ├── ui/                      # UI subsystem implementation
│   ├── search/                  # Search subsystem implementation
│   ├── preview/                 # Preview subsystem implementation
│   └── diff/                    # Diff subsystem implementation
└── tests/                        # Unit tests
```

---

## Build Instructions

### Step 1: Install Dependencies via vcpkg

```powershell
# Set environment variable if not already set
$env:VCPKG_ROOT = "C:\Dev\vcpkg"

cd $env:VCPKG_ROOT

# Install dependencies for x64-debug
.\vcpkg install ^
    nlohmann-json:x64-windows ^
    spdlog:x64-windows ^
    imgui:x64-windows ^
    glfw3:x64-windows ^
    stb:x64-windows ^
    directx-headers:x64-windows ^
    nanosvg:x64-windows

# Install dependencies for x64-release
.\vcpkg install ^
    nlohmann-json:x64-windows ^
    spdlog:x64-windows ^
    imgui:x64-windows ^
    glfw3:x64-windows ^
    stb:x64-windows ^
    directx-headers:x64-windows ^
    nanosvg:x64-windows
```

### Step 2: Configure CMake

```powershell
cd c:\Users\Ken\cpp\opacity

# Create build directory
mkdir build
cd build

# Configure for Visual Studio 2022 (x64)
cmake -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    ..

# Or for Visual Studio 2019 (x64)
cmake -G "Visual Studio 16 2019" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    ..
```

### Step 3: Build the Project

```powershell
# Build Debug configuration
cmake --build . --config Debug --parallel 4

# Build Release configuration
cmake --build . --config Release --parallel 4

# Or open Visual Studio solution
start .\Opacity.sln
```

### Step 4: Run the Application

```powershell
# From build directory
.\bin\Debug\opacity.exe

# Or
.\bin\Release\opacity.exe
```

---

## Automated Build Script

Create a `build.ps1` script for convenience:

```powershell
# save as: build.ps1

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [string]$VcpkgRoot = "C:\Dev\vcpkg",
    
    [switch]$Clean,
    
    [switch]$OpenSolution
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Opacity Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Configuration: $Configuration"
Write-Host "VcpkgRoot: $VcpkgRoot"

# Check vcpkg
if (-not (Test-Path $VcpkgRoot)) {
    Write-Host "ERROR: vcpkg not found at $VcpkgRoot" -ForegroundColor Red
    exit 1
}

# Clean build directory if requested
if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force (Join-Path $ScriptDir "build") -ErrorAction SilentlyContinue
}

# Create build directory
$BuildDir = Join-Path $ScriptDir "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Configure with CMake
Write-Host "Configuring CMake..." -ForegroundColor Yellow
Push-Location $BuildDir

cmake -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows `
    ..

if ($LASTEXITCODE -ne 0) {
    Write-Host "CMAKE configuration failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

# Build project
Write-Host "Building project..." -ForegroundColor Yellow
cmake --build . --config $Configuration --parallel 4

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

Write-Host "Build successful!" -ForegroundColor Green

# Open Visual Studio if requested
if ($OpenSolution) {
    Write-Host "Opening Visual Studio solution..." -ForegroundColor Yellow
    Start-Process "Opacity.sln"
}

Pop-Location
```

Usage:
```powershell
# Debug build
.\build.ps1 -Configuration Debug

# Release build with solution opened
.\build.ps1 -Configuration Release -OpenSolution

# Clean build
.\build.ps1 -Configuration Debug -Clean
```

---

## vcpkg Dependency Management

### Adding New Dependencies

Edit `vcpkg.json`:
```json
{
  "version": 3,
  "port-version": 0,
  "dependencies": [
    "existing-package",
    "new-package-name"
  ]
}
```

Then install:
```powershell
.\vcpkg install new-package-name:x64-windows
```

### Common Phase-Based Dependencies

**Phase 1 (Core):**
- nlohmann-json (configuration)
- spdlog (logging)
- imgui (UI framework)
- glfw3 (windowing, optional for native Win32)
- stb (image loading)

**Phase 2 (Media):**
- ffmpeg (video/audio)
- miniaudio (audio)

**Phase 3 (Advanced):**
- libzip (archive handling)
- zlib (compression)

**Phase 4+ (Future):**
- curl (network operations)
- openssl (security)

---

## Troubleshooting

### CMake Not Found
```powershell
# Verify CMake installation
cmake --version

# Add to PATH if needed
$env:PATH += ";C:\Program Files\CMake\bin"
[System.Environment]::SetEnvironmentVariable("PATH", $env:PATH, "User")
```

### vcpkg Issues
```powershell
# Clean vcpkg package cache
cd $env:VCPKG_ROOT
.\vcpkg remove --outdated

# Reinstall specific package
.\vcpkg remove nlohmann-json:x64-windows
.\vcpkg install nlohmann-json:x64-windows
```

### Visual Studio Not Found
```powershell
# List available Visual Studio installations
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -all
```

### Build Errors
1. Ensure all vcpkg dependencies are installed
2. Clear CMake cache: `rm -r build\CMakeFiles`
3. Reconfigure: `cmake .. -G "Visual Studio 17 2022"`

---

## Development Workflow

### During Development
1. Edit code in `src/` or `include/`
2. Rebuild: `cmake --build . --config Debug`
3. Run tests: `ctest -C Debug`
4. Commit changes

### Phase Completion
1. Ensure all tests pass
2. Verify no memory leaks (Debug build profiling)
3. Create release build: `cmake --build . --config Release`
4. Update version in `CMakeLists.txt`
5. Tag release: `git tag v1.0.0-phase1`

---

## Performance Testing

```powershell
# Release build performance test
.\build\bin\Release\opacity.exe --benchmark

# Debug build with profiling
# Use Visual Studio profiler: Debug > Performance Profiler
```

---

## Next Steps After Build Environment Setup

1. ✅ Build environment configured
2. ⬜ Start Phase 1 implementation (see phased-development-plan.md)
3. ⬜ Implement core subsystems
4. ⬜ Add UI framework integration
5. ⬜ Begin Phase 1 testing

For detailed Phase 1 implementation guidance, see `_plan/phased-development-plan.md`.
