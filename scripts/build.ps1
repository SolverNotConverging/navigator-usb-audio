param(
    [string]$ToolsRoot = 'C:/AMDDesignTools/2025.2',
    [switch]$Hardware,
    [switch]$Platform,
    [switch]$Program
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    if ($Hardware) {
        & "$ToolsRoot/Vivado/bin/vivado.bat" -mode batch -source scripts/build_hardware.tcl -log build_hardware.log -journal build_hardware.jou
        if ($LASTEXITCODE) { throw 'Hardware build failed' }
    }
    if ($Hardware -or $Platform -or !(Test-Path vitis/audio_platform/ps7_cortexa9_0/baremetal/bsp/lib/libxil.a)) {
        & "$ToolsRoot/Vitis/bin/vitis.bat" -s scripts/build_platform.py
        if ($LASTEXITCODE) { throw 'Platform build failed' }
    }
    & "$PSScriptRoot/compile_firmware.ps1" -ToolsRoot $ToolsRoot
    if ($Program) {
        & "$ToolsRoot/Vitis/bin/xsct.bat" scripts/program.tcl
        if ($LASTEXITCODE) { throw 'Programming failed' }
    }
} finally { Pop-Location }
