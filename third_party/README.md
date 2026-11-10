# AMD USB driver

`usbps/` contains unmodified source files from the AMD Vitis 2025.2
`usbps_v2_10` standalone USB driver. They are compiled as C and linked into
the C++ application. The generated platform supplies the other AMD libraries.

Upstream: https://github.com/Xilinx/embeddedsw/tree/master/XilinxProcessorIPLib/drivers/usbps

The files retain their copyright notices and MIT SPDX identifiers. See
`LICENSE.amd.txt` for the upstream license. The application corrects the ISO
queue-head Mult field after driver configuration in `firmware/usb_audio.cpp`;
the copied driver itself is unchanged.
