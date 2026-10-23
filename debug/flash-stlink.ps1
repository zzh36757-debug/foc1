param(
    [string]$ElfPath,
    [ValidateSet('STM32F103C8', 'STM32F407ZG')]
    [string]$Target = 'STM32F103C8',
    [ValidateSet('Foc1', 'Generic')]
    [string]$Board,
    [switch]$ValidateOnly,
    [switch]$ConnectOnly,
    [ValidateSet(100, 240, 480, 950, 1200, 1800, 4000)]
    [int]$FrequencyKhz = 240,
    [string]$StlinkBin = (Join-Path $env:USERPROFILE 'tools\stlink-1.9.0\stlink-1.9.0-x86_64-w64-mingw32\bin'),
    [string]$ObjcopyPath = (Join-Path $env:USERPROFILE '.eide\tools\gcc_arm\bin\arm-none-eabi-objcopy.exe')
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
# Templates keep this script inside .vscode/debug; the active F1 project uses debug/.
if ((Split-Path -Leaf $projectRoot) -like '.vscode*') { $projectRoot = Split-Path -Parent $projectRoot }
if (-not $ElfPath) { $ElfPath = Join-Path $projectRoot 'build\Debug\foc1.elf' }
if (-not $Board) { $Board = if ($Target -eq 'STM32F103C8') { 'Foc1' } else { 'Generic' } }
if ($Board -eq 'Foc1' -and $Target -ne 'STM32F103C8') {
    throw 'The Foc1 PA3 motor-stop behavior is only valid for the STM32F103C8 motor board.'
}
# Keep MCU memory and identification separate from board-specific GPIO behavior.
switch ($Target) {
    'STM32F103C8' {
        $expectedChipId = 0x410
        $expectedFlashKb = 64
        $flashSizeWordAddress = 0x1FFFF7E0
        $flashSizeShift = 0
        $stackRegions = @(@{ Start = 0x20000000; End = 0x20005000 })
    }
    'STM32F407ZG' {
        $expectedChipId = 0x413
        $expectedFlashKb = 1024
        # F407 Flash-size halfword is at 0x1FFF7A22: read the aligned word, then shift.
        $flashSizeWordAddress = 0x1FFF7A20
        $flashSizeShift = 16
        $stackRegions = @(@{ Start = 0x20000000; End = 0x20020000 },
                          @{ Start = 0x10000000; End = 0x10010000 })
    }
}
$flashBase = 0x08000000
$flashBytes = $expectedFlashKb * 1024
$ElfPath = (Resolve-Path -LiteralPath $ElfPath).Path
$binPath = [System.IO.Path]::ChangeExtension($ElfPath, '.bin')
$dllPath = Join-Path $StlinkBin 'libstlink.dll'
if (-not (Test-Path -LiteralPath $dllPath)) {
    throw "Missing stlink 1.9.0 library: $dllPath. See $PSScriptRoot/README.md."
}
if ([IntPtr]::Size -ne 8) { throw 'Use 64-bit PowerShell with the x64 stlink library.' }
& $ObjcopyPath -O binary $ElfPath $binPath
if ($LASTEXITCODE -ne 0) { throw 'ELF to binary conversion failed.' }
$image = [System.IO.File]::ReadAllBytes($binPath)
if ($image.Length -lt 8 -or $image.Length -gt $flashBytes -or ($image.Length % 2) -ne 0) {
    throw "Expected an even-sized $Target image of at most $expectedFlashKb KiB."
}
$initialSp = [BitConverter]::ToUInt32($image, 0)
$resetVector = [BitConverter]::ToUInt32($image, 4)
$stackValid = $false
foreach ($region in $stackRegions) {
    if ($initialSp -gt $region.Start -and $initialSp -le $region.End) { $stackValid = $true }
}
if (-not $stackValid -or ($initialSp % 8) -ne 0 -or
    $resetVector -lt ($flashBase + 1) -or $resetVector -ge ($flashBase + $image.Length) -or
    ($resetVector -band 1) -eq 0) {
    throw "Image vector table does not match $Target."
}
if ($ValidateOnly) {
    Write-Output "STLINK_IMAGE_VALID target=$Target board=$Board bytes=$($image.Length) flash=$($expectedFlashKb)KiB HARDWARE_NOT_ACCESSED"
    return
}

# Reuse upstream stlink's synchronous loader and full readback verification.
# The standard st-flash CLI resumes the application after writing; this wrapper
# calls the library directly so the motor board remains halted for Cortex-Debug.
if (-not ('FocStlink.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace FocStlink {
    public static class Native {
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        public static extern bool SetDllDirectory(string path);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        public static extern void init_chipids([MarshalAs(UnmanagedType.LPStr)] string path);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        public static extern IntPtr stlink_open_usb(int verbosity, int connect, IntPtr serial, int frequency);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        public static extern int stlink_target_connect(IntPtr probe, int connect);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        public static extern int stlink_force_debug(IntPtr probe);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool stlink_is_core_halted(IntPtr probe);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        public static extern int stlink_read_debug32(IntPtr probe, uint address, out uint value);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        public static extern int stlink_write_debug32(IntPtr probe, uint address, uint value);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        public static extern int stlink_write_flash(IntPtr probe, uint address, [In] byte[] data, uint length, byte eraseOnly, int eraseType);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        public static extern int stlink_reset(IntPtr probe, int resetType);
        [DllImport("libstlink.dll", CallingConvention=CallingConvention.Cdecl)]
        public static extern void stlink_close(IntPtr probe);
    }
}
'@
}
if (-not [FocStlink.Native]::SetDllDirectory($StlinkBin)) { throw 'Could not set stlink DLL search directory.' }
$env:PATH = $StlinkBin + ';' + $env:PATH
[FocStlink.Native]::init_chipids((Join-Path (Split-Path -Parent $StlinkBin) 'share\stlink\config\chips'))
$attemptFrequencies = @($FrequencyKhz)
if ($FrequencyKhz -ne 100) { $attemptFrequencies += 100 }
foreach ($attemptKhz in $attemptFrequencies) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $probe = [IntPtr]::Zero
    $motorBoard = $false
    try {
        # CONNECT_HOT_PLUG=0: do not pulse an unconnected NRST or start the application.
        $probe = [FocStlink.Native]::stlink_open_usb(50, 0, [IntPtr]::Zero, $attemptKhz)
        if ($probe -eq [IntPtr]::Zero) { throw 'ST-Link connection failed.' }
        # open_usb can return a USB handle even when MCU discovery failed.
        # Check target_connect explicitly before treating register data as a chip ID.
        if ([FocStlink.Native]::stlink_target_connect($probe, 0) -ne 0) {
            throw 'SWD target connection failed. ST-Link USB is present, but a valid STM32 CPU was not reached. No Flash was written.'
        }
        [uint32]$cpuId = 0
        if ([FocStlink.Native]::stlink_read_debug32($probe, 0xE000ED00L, [ref]$cpuId) -ne 0 -or
            ($cpuId -shr 24) -ne 0x41 -or (($cpuId -shr 16) -band 0xF) -ne 0xF) {
            throw ('SWD returned an invalid ARM CPUID (0x{0:X8}). This is a connection/read failure, not an MCU model identification. No Flash was written.' -f $cpuId)
        }
        [uint32]$chipId = 0
        [uint32]$flashSize = 0
        if ([FocStlink.Native]::stlink_read_debug32($probe, 0xE0042000L, [ref]$chipId) -ne 0 -or
            [FocStlink.Native]::stlink_read_debug32($probe, $flashSizeWordAddress, [ref]$flashSize) -ne 0) {
            throw 'Could not read MCU identification.'
        }
        $actualFlashKb = ($flashSize -shr $flashSizeShift) -band 0xFFFF
        Write-Output ('STLINK_TARGET_READ cpu=0x{0:X8} idcode=0x{1:X8} flash={2}KiB SWD={3}kHz' -f $cpuId, $chipId, $actualFlashKb, $attemptKhz)
        if (($chipId -band 0xFFF) -lt 0x400 -or $actualFlashKb -eq 0 -or $actualFlashKb -eq 0xFFFF) {
            throw 'SWD chip identification was incomplete or invalid. No Flash was written.'
        }
        if (($chipId -band 0xFFF) -ne $expectedChipId -or $actualFlashKb -ne $expectedFlashKb) {
            throw ('Connected MCU differs from {0}: actual device ID 0x{1:X3}, Flash {2} KiB; expected ID 0x{3:X3}, Flash {4} KiB. No Flash was written.' -f $Target, ($chipId -band 0xFFF), $actualFlashKb, $expectedChipId, $expectedFlashKb)
        }
        if ([FocStlink.Native]::stlink_force_debug($probe) -ne 0 -or
            -not [FocStlink.Native]::stlink_is_core_halted($probe)) {
            throw 'Could not halt the MCU. No Flash was written.'
        }
        $motorBoard = ($Board -eq 'Foc1')
        if ($motorBoard -and [FocStlink.Native]::stlink_write_debug32($probe, 0x40010814, 8) -ne 0) {
            throw 'Could not clear DRV_EN.'
        }
        if ($ConnectOnly) {
            Write-Output "STLINK_TARGET_READY target=$Target board=$Board CPU=halted FLASH_NOT_WRITTEN"
            break
        }
        # Begin programming from a reset state; keep the application halted.
        if ([FocStlink.Native]::stlink_reset($probe, 3) -ne 0 -or
            -not [FocStlink.Native]::stlink_is_core_halted($probe)) {
            throw 'Could not reset and halt the MCU before programming.'
        }
        Write-Output "STLINK_FLASH_BEGIN target=$Target board=$Board bytes=$($image.Length) SWD=$($attemptKhz)kHz"
        # SECTION_ERASE=1: only erase image pages. The library verifies every byte.
        if ([FocStlink.Native]::stlink_write_flash($probe, $flashBase, $image, $image.Length, 0, 1) -ne 0) {
            throw 'ST-Link write or full readback verification failed.'
        }
        # RESET_SOFT_AND_HALT=3: prepare the reset vector without an NRST wire.
        if ([FocStlink.Native]::stlink_reset($probe, 3) -ne 0 -or
            [FocStlink.Native]::stlink_force_debug($probe) -ne 0 -or
            -not [FocStlink.Native]::stlink_is_core_halted($probe)) {
            throw 'Write verified, but could not leave the MCU halted.'
        }
        $boardState = ''
        if ($motorBoard) {
            [uint32]$portA = 0
            if ([FocStlink.Native]::stlink_write_debug32($probe, 0x40010814, 8) -ne 0 -or
                [FocStlink.Native]::stlink_read_debug32($probe, 0x4001080C, [ref]$portA) -ne 0 -or ($portA -band 8) -ne 0) {
                throw 'Write verified, but DRV_EN low could not be confirmed.'
            }
            $boardState = ' DRV_EN=0'
        }
        $timer.Stop()
        Write-Output ('STLINK_FLASH_VERIFIED target={0} bytes={1} SWD={2}kHz elapsed={3:F2}s CPU=halted{4}' -f $Target, $image.Length, $attemptKhz, $timer.Elapsed.TotalSeconds, $boardState)
        break
    }
    catch {
        if ($attemptKhz -eq 100) { throw }
        $retryAction = if ($ConnectOnly) { 'Retrying target connection at 100kHz; Flash will not be written.' } else { 'Retrying the complete image with full verification at 100kHz.' }
        Write-Warning ("{0}kHz attempt failed: {1} {2}" -f $attemptKhz, $_.Exception.Message, $retryAction)
    }
    finally {
        if ($probe -ne [IntPtr]::Zero) {
            [void][FocStlink.Native]::stlink_force_debug($probe)
            if ($motorBoard) { [void][FocStlink.Native]::stlink_write_debug32($probe, 0x40010814, 8) }
            [FocStlink.Native]::stlink_close($probe)
        }
    }
}
