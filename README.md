# Navigator USB audio — C++17

This project turns the connected ALIENTEK Navigator Zynq-7020 board into a
48 kHz, 16-bit stereo USB playback device named **Navigator USB Audio**.

The application runs as C++17 on the Zynq's ARM Cortex-A9. Standard Vivado IP
in the FPGA generates the audio clocks and sends I2S to the onboard ES8388.
You can change the application without writing Verilog or rebuilding the FPGA.
This is a bare-metal C++ application, not an HLS project.

```text
Laptop USB audio -> PS USB0/USB3320 -> C++ sample buffer and volume
                 -> AXI FIFO -> FPGA I2S transmitter -> ES8388 -> PHONE
```

## Start here

| File | What you edit |
| --- | --- |
| `firmware/main.cpp` | Startup and main loop |
| `firmware/audio.cpp` | Samples, volume, buffering and clock feedback |
| `firmware/usb_audio.cpp` | USB requests and endpoint transfers |
| `firmware/codec.cpp` | ES8388 configuration over I2C |
| `firmware/descriptors.h` | Device name references, USB audio format and topology |
| `scripts/build_hardware.tcl` | Vivado hardware design |
| `constraints/board.xdc` | Board pin assignments |

AMD's low-level USB driver remains C in `third_party/usbps`; it links with the
C++ application. The application builds with exceptions and RTTI disabled.

## Use the board

1. Keep the board powered and connect the laptop to **USB_Slave**.
2. P4 must select the slave connector: links **1–3** and **2–4**. Use the
   PCB's pin-1 marking when locating these pins.
3. Connect headphones or powered speakers to the board's **PHONE** socket.
4. In Windows sound output, select **Speakers (Navigator USB Audio)**.

USB_UART/COM5 is a separate serial connection. JTAG is used to load the project.
The onboard speaker is disabled in this configuration. Firmware volume starts
at -12 dB and codec analog output at -15 dB; Windows can restore its saved volume.

## Build and load

Tools used: AMD Vivado and Vitis **2025.2**. Python is needed only for the optional
host tests. In PowerShell, replace the two placeholder paths below with an
appropriate project location and your AMD tools installation directory. The
tools directory must contain the `Vivado` and `Vitis` subdirectories.

```powershell
# Choose paths appropriate for your computer before running these commands.
$projectPath = 'C:/path/to/your/project'
$toolsRoot = 'C:/path/to/AMD/tools/2025.2'

# First build after cloning from GitHub into your chosen directory.
git clone https://github.com/SolverNotConverging/navigator-usb-audio.git $projectPath
Set-Location $projectPath
./scripts/build.ps1 -ToolsRoot $toolsRoot -Hardware -Program

# Usual edit/build/load cycle; uses the already-built hardware and platform.
./scripts/build.ps1 -ToolsRoot $toolsRoot -Program

# Compile C++ only, without changing the running board.
./scripts/build.ps1 -ToolsRoot $toolsRoot

# Full rebuild from the checked-in source scripts, then load.
./scripts/build.ps1 -ToolsRoot $toolsRoot -Hardware -Program
```

If you already have the project locally, set `$projectPath` to its existing
directory and skip the clone step. Generated hardware, BSPs and binaries are
excluded from the repository; the first build regenerates them.

Use `-Platform` to regenerate the Vitis platform from an existing `usb_audio.xsa`.
The scripts check command exit codes. A hardware build takes several minutes.

Generated outputs:

- `hardware/usb_audio.xpr`: open in Vivado to inspect the block design.
- `usb_audio.xsa`: hardware export used by the Vitis platform.
- `vitis/audio_platform`: generated standalone BSP/platform.
- `build/usb_audio.elf`: compiled C++ application.

Loading programs the FPGA and ARM through JTAG. It is temporary and must be
repeated after a power cycle. These scripts do not install a boot image.

## Playback and diagnostics

```powershell
python tests/test_descriptors.py
python scripts/test_playback.py --seconds 5
python scripts/test_playback.py --seconds 60 --silence
& "$toolsRoot/Vitis/bin/xsct.bat" scripts/status.tcl
```

The playback script selects Navigator explicitly. Its audible test sends a quiet
440 Hz left tone and 660 Hz right tone, with short fades. It waits up to 15 seconds
for Windows to enumerate the device and make its audio driver ready after loading.

`stage=3`, `codec_ok=1` and `configured=1` indicate successful startup. During
playback, `alternate=1`, `packets`, `played_frames` and `feedback_packets` should
increase. Buffer and USB error counters should remain zero during a continuous
stream. An `underruns` increment can also record the host stopping its packets
before closing the streaming interface. `i2s_errors` counts transmitter starvation
while playing. Diagnostics accumulate until the next firmware load.

## Implementation details

- USB Audio Class 1, full-speed asynchronous OUT endpoint `0x01`; explicit
  3-byte 10.14 feedback on endpoint `0x81`. Fixed 48 kHz stereo PCM.
- Master mute and volume from -60 dB to 0 dB in 1 dB steps.
- 4096-frame ring with a 1024-frame startup target (about 21 ms), plus hardware
  and Windows buffering. Feedback adjusts the host rate around 48 frames/ms.
- 12.288 MHz MCLK, 3.072 MHz BCLK and nominal 48 kHz LRCLK.
- AXI IIC `0x41600000`, TX FIFO `0x43C00000`, I2S TX `0x43C10000`.
- USB0 uses MIO28–39 with USB3320 reset on MIO9. Firmware and USB DMA use OCM;
  external DDR is disabled. Data cache is disabled for DMA; instruction cache is on.
- FPGA part: `xc7z020clg400-2`. Board pin map targets Navigator v3.9.
- VID/PID `03FD:0200` comes from an AMD development example. Assign suitable IDs
  before distributing a product.

USB endpoint descriptors and actual hardware playback are checked separately.
Hardware timing passed with setup slack +1.966 ns and hold slack +0.053 ns.
Live checks initialized the codec and transferred audio. A one-minute stream
reported zero USB, malformed-packet, ring-overflow, ring-underflow and I2S errors.
Other runs also transferred feedback and adjusted the host packet rate.

**Remaining bring-up issue:** the first playback after some JTAG reloads leaves
`feedback_packets=0`, despite audio OUT packets arriving normally. Stopping and
reopening playback restored feedback in testing. This startup behavior is not yet
resolved; long-session clock synchronization is therefore not validated. Check
that `feedback_packets` increases during playback before relying on extended use.

Listening at the PHONE socket still needs confirmation; software counters alone
do not establish analog sound quality. This is a working development project,
with the startup limitation above, rather than a finished USB audio product.

## References

- [AMD USB standalone driver](https://github.com/Xilinx/embeddedsw/tree/master/XilinxProcessorIPLib/drivers/usbps)
- [Zynq endpoint queue-head fields](https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM/Endpoint-Queue-Head-Descriptor-dQH)
- [AMD I2S product guide](https://docs.amd.com/r/en-US/pg308-i2s)
- [TinyUSB UAC1 descriptor definitions](https://github.com/hathach/tinyusb/blob/master/src/device/usbd.h)

The firmware sets ISO queue-head `Mult=1`, as required by the controller, after
the AMD driver initializes the queues. Feedback is queued before acknowledging
the streaming alternate setting so the first host feedback poll has data ready.

## License

This project is available under the [MIT License](LICENSE). Included AMD driver
files retain their own copyright notices and [MIT license](third_party/LICENSE.amd.txt).
AMD development tools and generated platform libraries remain subject to their
respective licenses.
