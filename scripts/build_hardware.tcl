# Vivado 2025.2: Navigator USB stereo audio playback. Run from any directory.
set root [file normalize [file join [file dirname [info script]] ..]]
set jobs 4
create_project -force usb_audio $root/hardware -part xc7z020clg400-2
set_property target_language Verilog [current_project]
create_bd_design audio_system
proc ip {name vlnv props} {
    set matches [get_ipdefs -all -quiet $vlnv]
    if {![llength $matches]} {
        set family [join [lrange [split $vlnv :] 0 2] :]
        set vlnv [lindex [lsort -dictionary [get_ipdefs -all ${family}:*]] end]
    }
    set c [create_bd_cell -type ip -vlnv $vlnv $name]
    if {[llength $props]} {set_property -dict $props $c}
    return $c
}
proc wire {args} {
    set pins {}
    foreach p $args {lappend pins [get_bd_pins $p]}
    connect_bd_net {*}$pins
}
ip processing_system7_0 xilinx.com:ip:processing_system7:5.5 [list \
 CONFIG.PCW_EN_DDR 0 CONFIG.PCW_UIPARAM_DDR_ENABLE 0 \
 CONFIG.PCW_USB0_PERIPHERAL_ENABLE 1 CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39} \
 CONFIG.PCW_USB0_RESET_ENABLE 1 CONFIG.PCW_USB0_RESET_IO {MIO 9} \
 CONFIG.PCW_USE_M_AXI_GP0 1 CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ 50 \
 CONFIG.PCW_EN_CLK0_PORT 1 CONFIG.PCW_EN_RST0_PORT 1 \
 CONFIG.PCW_PRESET_BANK0_VOLTAGE {LVCMOS 3.3V} \
 CONFIG.PCW_PRESET_BANK1_VOLTAGE {LVCMOS 1.8V}]
make_bd_intf_pins_external [get_bd_intf_pins processing_system7_0/FIXED_IO]
# 64 MHz / (16 * UART divisor 16) = exactly 250 kbaud.
ip bus_clock xilinx.com:ip:clk_wiz:6.0 [list CONFIG.PRIM_IN_FREQ 50 CONFIG.CLKOUT1_REQUESTED_OUT_FREQ 64 CONFIG.USE_RESET true CONFIG.RESET_TYPE ACTIVE_LOW CONFIG.PRIM_SOURCE No_buffer]
wire processing_system7_0/FCLK_CLK0 bus_clock/clk_in1
wire processing_system7_0/FCLK_RESET0_N bus_clock/resetn
wire bus_clock/clk_out1 processing_system7_0/M_AXI_GP0_ACLK
ip axi_uart16550_0 xilinx.com:ip:axi_uart16550:2.0 [list CONFIG.C_IS_A_16550 16550 CONFIG.C_HAS_EXTERNAL_XIN 0 CONFIG.C_HAS_EXTERNAL_RCLK 0 CONFIG.C_USE_MODEM_PORTS 0 CONFIG.C_USE_USER_PORTS 0]
ip axi_iic_0 xilinx.com:ip:axi_iic:2.0 [list CONFIG.IIC_FREQ_KHZ 100]
ip audio_fifo xilinx.com:ip:axi_fifo_mm_s:4.2 [list CONFIG.C_USE_TX_DATA 1 CONFIG.C_USE_RX_DATA 0 CONFIG.C_USE_TX_CTRL 0 CONFIG.C_TX_FIFO_DEPTH 512 CONFIG.C_USE_TX_CUT_THROUGH 0]
ip i2s_tx xilinx.com:ip:i2s_transmitter:1.0 [list CONFIG.C_NUM_CHANNELS 2 CONFIG.C_DWIDTH 16 CONFIG.C_32BIT_LR 1 CONFIG.C_DEPTH 128]
foreach c {axi_uart16550_0 axi_iic_0 audio_fifo i2s_tx} {
    set intf S_AXI
    if {$c eq "i2s_tx"} {set intf S_AXI_CTRL}
    apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {
        Master {/processing_system7_0/M_AXI_GP0} Clk_master {/bus_clock/clk_out1 (64 MHz)}
        Clk_slave {/bus_clock/clk_out1 (64 MHz)} Clk_xbar {/bus_clock/clk_out1 (64 MHz)}
    } [get_bd_intf_pins $c/$intf]
}
create_bd_port -dir I UART_rxd
create_bd_port -dir O UART_txd
connect_bd_net [get_bd_ports UART_rxd] [get_bd_pins axi_uart16550_0/sin]
connect_bd_net [get_bd_ports UART_txd] [get_bd_pins axi_uart16550_0/sout]
make_bd_intf_pins_external [get_bd_intf_pins axi_iic_0/IIC]
set_property name codec_iic [get_bd_intf_ports IIC_0]

# No HDL adapter: split a software-tagged FIFO word into payload and channel ID.
ip audio_payload xilinx.com:ip:xlslice:1.0 [list CONFIG.DIN_WIDTH 32 CONFIG.DIN_FROM 27 CONFIG.DIN_TO 0 CONFIG.DOUT_WIDTH 28]
ip audio_channel xilinx.com:ip:xlslice:1.0 [list CONFIG.DIN_WIDTH 32 CONFIG.DIN_FROM 31 CONFIG.DIN_TO 29 CONFIG.DOUT_WIDTH 3]
ip metadata_zero xilinx.com:ip:xlconstant:1.1 [list CONFIG.CONST_WIDTH 4 CONFIG.CONST_VAL 0]
ip audio_word xilinx.com:ip:xlconcat:2.1 [list CONFIG.NUM_PORTS 2 CONFIG.IN0_WIDTH 28 CONFIG.IN1_WIDTH 4]
wire audio_fifo/axi_str_txd_tdata audio_payload/Din audio_channel/Din
wire audio_payload/Dout audio_word/In0
wire metadata_zero/dout audio_word/In1
wire audio_word/dout i2s_tx/s_axis_aud_tdata
wire audio_channel/Dout i2s_tx/s_axis_aud_tid
wire audio_fifo/axi_str_txd_tvalid i2s_tx/s_axis_aud_tvalid
wire audio_fifo/axi_str_txd_tready i2s_tx/s_axis_aud_tready
wire bus_clock/clk_out1 i2s_tx/s_axis_aud_aclk
set rst [lindex [get_bd_cells -hier -filter {VLNV =~ "xilinx.com:ip:proc_sys_reset:*"}] 0]
wire $rst/peripheral_aresetn i2s_tx/s_axis_aud_aresetn
# AXI automation connects the reset controller to bus_clock/locked.

# 64 MHz allows exact nominal 12.288 MHz (M=12, D=1, O=62.5).
ip audio_clock xilinx.com:ip:clk_wiz:6.0 [list CONFIG.PRIM_IN_FREQ 64 CONFIG.CLKOUT1_REQUESTED_OUT_FREQ 12.288 CONFIG.USE_RESET true CONFIG.RESET_TYPE ACTIVE_LOW CONFIG.PRIM_SOURCE No_buffer]
wire bus_clock/clk_out1 audio_clock/clk_in1
wire processing_system7_0/FCLK_RESET0_N audio_clock/resetn
ip audio_reset xilinx.com:ip:proc_sys_reset:5.0 {}
wire audio_clock/clk_out1 i2s_tx/aud_mclk audio_reset/slowest_sync_clk
wire audio_clock/locked audio_reset/dcm_locked
wire processing_system7_0/FCLK_RESET0_N audio_reset/ext_reset_in
wire audio_reset/peripheral_reset i2s_tx/aud_mrst
foreach {pin port} {audio_clock/clk_out1 aud_mclk i2s_tx/sclk_out aud_bclk i2s_tx/lrclk_out aud_lrc i2s_tx/sdata_0_out aud_dacdat} {
    set p [create_bd_port -dir O $port]
    connect_bd_net [get_bd_pins $pin] $p
}
assign_bd_address
foreach {cell addr} {axi_uart16550_0 0x42C00000 axi_iic_0 0x41600000 audio_fifo 0x43C00000 i2s_tx 0x43C10000} {
    set seg [get_bd_addr_segs -of_objects [get_bd_addr_spaces processing_system7_0/Data] -filter "NAME =~ *${cell}*"]
    set_property offset $addr $seg
    set_property range 64K $seg
}
validate_bd_design
save_bd_design
generate_target all [get_files audio_system.bd]
make_wrapper -files [get_files audio_system.bd] -top
add_files -norecurse $root/hardware/usb_audio.gen/sources_1/bd/audio_system/hdl/audio_system_wrapper.v
set_property top audio_system_wrapper [current_fileset]
add_files -fileset constrs_1 $root/constraints/board.xdc
add_files -fileset constrs_1 $root/constraints/timing.xdc
set_property USED_IN_SYNTHESIS false [get_files timing.xdc]
write_bd_tcl -force $root/hardware/recreate_bd.tcl
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "Synthesis failed"}
launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Implementation failed"}
open_run impl_1
report_timing_summary -file $root/hardware/final_timing.rpt
report_drc -file $root/hardware/final_drc.rpt
write_hw_platform -fixed -include_bit -force -file $root/usb_audio.xsa
puts "USB_AUDIO_BUILD_SUCCESS"
