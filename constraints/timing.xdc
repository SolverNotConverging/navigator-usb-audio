set rx_first [all_fanout -flat -endpoints_only -from [get_ports UART_rxd]]
set_false_path -from [get_ports UART_rxd] -to $rx_first
set scl_first [all_fanout -flat -endpoints_only -from [get_ports codec_iic_scl_io]]
set_false_path -from [get_ports codec_iic_scl_io] -to $scl_first
set sda_first [all_fanout -flat -endpoints_only -from [get_ports codec_iic_sda_io]]
set_false_path -from [get_ports codec_iic_sda_io] -to $sda_first
set tx_start [all_fanin -flat -startpoints_only -to [get_ports UART_txd]]
set_max_delay -datapath_only 10 -from $tx_start -to [get_ports UART_txd]
set scl_start [all_fanin -flat -startpoints_only -to [get_ports codec_iic_scl_io]]
set_max_delay -datapath_only 10 -from $scl_start -to [get_ports codec_iic_scl_io]
set sda_start [all_fanin -flat -startpoints_only -to [get_ports codec_iic_sda_io]]
set_max_delay -datapath_only 10 -from $sda_start -to [get_ports codec_iic_sda_io]
# Firmware sets MCLK/(2*2) = BCLK, with 64 BCLKs per stereo frame.
create_generated_clock -name codec_bclk -source [get_pins audio_system_i/audio_clock/inst/mmcm_adv_inst/CLKOUT0] -divide_by 4 [get_ports aud_bclk]
# ES8388 samples on BCLK rising edges; TX changes data at falling edges.
set_output_delay -clock codec_bclk -max 20 [get_ports {aud_lrc aud_dacdat}]
set_output_delay -clock codec_bclk -min 0 [get_ports {aud_lrc aud_dacdat}]
