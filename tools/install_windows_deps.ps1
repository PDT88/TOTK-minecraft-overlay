# One-click Windows build & runtime dependency installer for TOTK Minecraft Overlay.
# Run from PowerShell:
#   powershell -ExecutionPolicy Bypass -File tools\install_windows_deps.ps1

$ErrorActionPreference = 'Stop'

function Install-WingetPackage {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [string]$OverrideArgs = ''
    )
    Write-Host "`n==> Installing $Id via winget..."
    $wingetArgs = @(
        'install',
        '--accept-package-agreements',
        '--accept-source-agreements',
        '-e',
        '--id', $Id
    )
    if ($OverrideArgs) {
        $wingetArgs += @('--override', $OverrideArgs)
    }
    & winget @wingetArgs
    $code = $LASTEXITCODE
    # 0 = installed; -1978335189 (0x8A15002B) / -1978335212 (0x8A150014) = already installed / no upgrade available
    if ($code -ne 0 -and $code -ne -1978335189 -and $code -ne -1978335212) {
        throw "winget install $Id failed with exit code $code"
    }
}

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget was not found. Install 'App Installer' from the Microsoft Store or follow docs\building.md manually."
}

Install-WingetPackage -Id 'astral-sh.uv'
Install-WingetPackage -Id 'Microsoft.DotNet.SDK.10'
Install-WingetPackage -Id 'KhronosGroup.VulkanSDK'
Install-WingetPackage -Id 'Gyan.FFmpeg'
Install-WingetPackage -Id 'Microsoft.VisualStudio.BuildTools' -OverrideArgs '--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
Install-WingetPackage -Id 'MSYS2.MSYS2'

# Add Visual Studio Installer to User PATH so .NET NativeAOT (tools\build_xlate.py) finds vswhere.exe
$vsInstaller = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
if (Test-Path $vsInstaller) {
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $parts = @()
    if ($userPath) {
        $parts = $userPath -split ';' | Where-Object { $_ -ne '' }
    }
    if ($parts -notcontains $vsInstaller) {
        $newUserPath = (($parts + $vsInstaller) -join ';')
        [Environment]::SetEnvironmentVariable('Path', $newUserPath, 'User')
        Write-Host "==> Added '$vsInstaller' to User PATH."
    }
    if (($env:PATH -split ';') -notcontains $vsInstaller) {
        $env:PATH = "$vsInstaller;$env:PATH"
    }
}

# Install MSYS2 UCRT64 toolchain and libraries headlessly
$msysBash = 'C:\msys64\usr\bin\bash.exe'
if (-not (Test-Path $msysBash)) {
    throw "MSYS2 bash not found at $msysBash after installing MSYS2.MSYS2."
}

Write-Host "`n==> Updating MSYS2 core and installing UCRT64 packages..."
$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
# First pass may terminate bash.exe if msys2-runtime updates; allow non-zero exit here.
$ErrorActionPreference = 'Continue'
& $msysBash -lc 'pacman -Syuu --noconfirm'
$ErrorActionPreference = 'Stop'
& $msysBash -lc 'pacman -Syuu --noconfirm'
& $msysBash -lc 'pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-opus mingw-w64-ucrt-x86_64-libvpx mingw-w64-ucrt-x86_64-zstd mingw-w64-ucrt-x86_64-vulkan-devel mingw-w64-ucrt-x86_64-vulkan-memory-allocator mingw-w64-ucrt-x86_64-vulkan-validation-layers mingw-w64-ucrt-x86_64-shaderc mingw-w64-ucrt-x86_64-spirv-tools base-devel git unzip'
if ($LASTEXITCODE -ne 0) {
    throw "MSYS2 UCRT64 package installation failed with exit code $LASTEXITCODE"
}

Write-Host "`nAll Windows dependencies are installed."
Write-Host "Open a NEW PowerShell window in this repo and run:"
Write-Host "  uv run --python 3.12 tools\setup_mc_windows.py --base C:\path\to\totk.xci --update C:\path\to\totk_update_1.2.1.nsp"
