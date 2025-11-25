#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Opacity project build automation script
.DESCRIPTION
    Builds the Opacity file manager project with CMake and vcpkg support
.PARAMETER Configuration
    Build configuration: Debug or Release (default: Debug)
.PARAMETER VcpkgRoot
    Path to vcpkg installation (default: $env:USERPROFILE\vcpkg)
.PARAMETER Clean
    Perform clean build (remove build directory)
.PARAMETER OpenSolution
    Open Visual Studio solution after build
.PARAMETER Install
    Install vcpkg dependencies before build
.PARAMETER Test
    Run tests after build
.EXAMPLE
    .\build.ps1
    # Debug build

    .\build.ps1 -Configuration Release -OpenSolution
    # Release build and open in Visual Studio

    .\build.ps1 -Clean -Install -Test
    # Clean build with dependencies, run tests
#>

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [string]$VcpkgRoot = "$env:USERPROFILE\vcpkg",
    
    [switch]$Clean,
    
    [switch]$OpenSolution,
    
    [switch]$Install,
    
    [switch]$Test
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ScriptDir "build"

# Color output functions
function Write-Title {
    param([string]$Text)
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host $Text -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Text)
    Write-Host "→ $Text" -ForegroundColor Yellow
}

function Write-Success {
    param([string]$Text)
    Write-Host "✓ $Text" -ForegroundColor Green
}

function Write-Error {
    param([string]$Text)
    Write-Host "✗ $Text" -ForegroundColor Red
}

# Main build process
try {
    Write-Title "Opacity Project Build"
    Write-Host "Configuration: $Configuration"
    Write-Host "vcpkg Root: $VcpkgRoot"
    Write-Host "Build Directory: $BuildDir"
    
    # Verify vcpkg
    if (-not (Test-Path $VcpkgRoot)) {
        Write-Error "vcpkg not found at $VcpkgRoot"
        Write-Host "Install vcpkg from: https://github.com/Microsoft/vcpkg" -ForegroundColor Yellow
        exit 1
    }
    Write-Success "vcpkg found"
    
    # Install dependencies if requested
    if ($Install) {
        Write-Step "Installing dependencies..."
        Push-Location $VcpkgRoot
        
        $packages = @(
            "nlohmann-json:x64-windows",
            "spdlog:x64-windows",
            "imgui:x64-windows",
            "glfw3:x64-windows",
            "stb:x64-windows",
            "directx-headers:x64-windows",
            "nanosvg:x64-windows"
        )
        
        foreach ($package in $packages) {
            Write-Host "  Installing $package..." -ForegroundColor Gray
            & .\vcpkg install $package --quiet 2>&1 | Out-Null
        }
        
        Pop-Location
        Write-Success "Dependencies installed"
    }
    
    # Clean build if requested
    if ($Clean) {
        Write-Step "Cleaning build directory..."
        if (Test-Path $BuildDir) {
            Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
        }
        Write-Success "Build directory cleaned"
    }
    
    # Create build directory
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
        Write-Success "Build directory created"
    }
    
    # Configure with CMake
    Write-Step "Configuring CMake..."
    Push-Location $BuildDir
    
    $vsVersion = "Visual Studio 17 2022"
    $cmakeArgs = @(
        "-G", $vsVersion,
        "-A", "x64",
        "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake",
        "-DVCPKG_TARGET_TRIPLET=x64-windows",
        ".."
    )
    
    & cmake @cmakeArgs
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configuration failed"
        Pop-Location
        exit 1
    }
    
    Write-Success "CMake configured"
    
    # Build project
    Write-Step "Building project (Configuration: $Configuration)..."
    & cmake --build . --config $Configuration --parallel 4 --verbose
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed"
        Pop-Location
        exit 1
    }
    
    Write-Success "Build completed successfully"
    
    # Run tests if requested
    if ($Test) {
        Write-Step "Running tests..."
        & ctest -C $Configuration -V
        
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Some tests failed"
            Pop-Location
            exit 1
        }
        
        Write-Success "All tests passed"
    }
    
    # Open Visual Studio if requested
    if ($OpenSolution) {
        Write-Step "Opening Visual Studio solution..."
        $slnFile = Join-Path $BuildDir "Opacity.sln"
        
        if (Test-Path $slnFile) {
            Start-Process $slnFile
            Write-Success "Visual Studio opened"
        }
        else {
            Write-Error "Solution file not found: $slnFile"
        }
    }
    
    Pop-Location
    
    Write-Title "Build Summary"
    Write-Host "Executable: $BuildDir\bin\$Configuration\opacity.exe"
    Write-Host "Build Configuration: $Configuration"
    Write-Host ""
    Write-Success "Build complete!"
    
    if ($Configuration -eq "Debug") {
        Write-Host "`nRun: .\build\bin\Debug\opacity.exe" -ForegroundColor Gray
    }
    else {
        Write-Host "`nRun: .\build\bin\Release\opacity.exe" -ForegroundColor Gray
    }
}
catch {
    Write-Error "Build script error: $_"
    exit 1
}
