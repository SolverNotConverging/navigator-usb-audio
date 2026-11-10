param(
    [string]$ToolsRoot = 'C:/AMDDesignTools/2025.2',
    [string]$BspRoot = '',
    [switch]$LegacyBsp
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!$BspRoot) { $BspRoot = Join-Path $projectRoot 'vitis/audio_platform/ps7_cortexa9_0/baremetal/bsp' }
$gcc = Join-Path $ToolsRoot 'Vitis/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe'
$gpp = $gcc -replace 'gcc.exe$', 'g++.exe'
$include = Join-Path $BspRoot 'include'
$lib = Join-Path $BspRoot 'lib'
if ($LegacyBsp) {
    $include = Join-Path $BspRoot 'bspinclude/include'
    $lib = Join-Path $BspRoot 'bsplib/lib'
}
if (!(Test-Path (Join-Path $lib 'libxil.a'))) { throw 'Build the platform first: vitis -s scripts/build_platform.py' }
$buildDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Force $buildDir | Out-Null
Push-Location $projectRoot
try {
    $flags = @('-Wall','-Wextra','-Os','-g3','-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=hard',
        '-ffunction-sections','-fdata-sections',"-I$include",'-Ithird_party/usbps','-Ifirmware')
    if (!$LegacyBsp) { $flags += '-DSDT' }
    $objects = @()
    foreach ($source in Get-ChildItem firmware -Filter '*.cpp') {
        $object = Join-Path $buildDir ($source.BaseName + '.o')
        & $gpp @flags '-std=c++17' '-fno-exceptions' '-fno-rtti' '-fno-threadsafe-statics' '-c' $source.FullName '-o' $object
        if ($LASTEXITCODE) { throw "C++ compilation failed: $($source.Name)" }
        $objects += $object
    }
    foreach ($source in Get-ChildItem third_party/usbps -Filter '*.c') {
        $object = Join-Path $buildDir ($source.BaseName + '.o')
        & $gcc @flags '-c' $source.FullName '-o' $object
        if ($LASTEXITCODE) { throw "AMD driver compilation failed: $($source.Name)" }
        $objects += $object
    }
    $libraries = '-Wl,--start-group,-lxil,-lm,-lgcc,-lc,--end-group'
    if (!$LegacyBsp) { $libraries = '-Wl,--start-group,-lxil,-lxilstandalone,-lxiltimer,-lm,-lgcc,-lc,--end-group' }
    & $gpp '-mcpu=cortex-a9' '-mfpu=vfpv3' '-mfloat-abi=hard' '-Wl,-build-id=none,--gc-sections' `
        '-specs=firmware/Xilinx.spec' '-Wl,-T,firmware/lscript.ld' '-Wl,-Map,build/usb_audio.map' `
        "-L$lib" '-o' 'build/usb_audio.elf' @objects $libraries
    if ($LASTEXITCODE) { throw 'Firmware link failed' }
    & ($gcc -replace 'gcc.exe$', 'size.exe') build/usb_audio.elf
    $symbol = & ($gcc -replace 'gcc.exe$', 'nm.exe') build/usb_audio.elf | Select-String ' [DB] diagnostics$'
    if (!$symbol) { throw 'Diagnostic symbol missing' }
    ($symbol.ToString().Split(' ')[0]) | Set-Content build/diagnostics_address.txt
} finally { Pop-Location }
