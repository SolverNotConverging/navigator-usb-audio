# Volatile load only. No QSPI/SD writes. Power cycling removes this program.
set root [file normalize [file join [file dirname [info script]] ..]]
connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name =~ "APU*"}
rst -system
after 1000
targets -set -nocase -filter {name =~ "*A9*#0"}
stop
targets -set -filter {name =~ "xc7z020*"}
fpga -file $root/hardware/usb_audio.runs/impl_1/audio_system_wrapper.bit
targets -set -nocase -filter {name =~ "APU*"}
loadhw -hw $root/usb_audio.xsa -mem-ranges [list {0x40000000 0xbfffffff}] -regs
source $root/build/ps7_init.tcl
ps7_init
ps7_post_config
targets -set -nocase -filter {name =~ "*A9*#0"}
rst -processor
dow $root/build/usb_audio.elf
con
puts "USB_AUDIO_RUNNING"
disconnect
exit
