set root [file normalize [file join [file dirname [info script]] ..]]
set f [open $root/build/diagnostics_address.txt r]
set base [expr 0x[string trim [read $f]]]
close $f
connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name =~ "APU*"}
set fields {magic stage codec_ok configured alternate usb_resets setup_requests packets received_frames played_frames underruns overruns malformed ring_frames feedback_q14 usb_errors last_setup last_value_index loops nonzero_frames feedback_packets i2s_errors feedback_result feedback_queued}
set values [mrd -value $base [llength $fields]]
foreach field $fields value $values {
    puts [format "%-20s %10u (0x%08X)" $field $value $value]
}
foreach {field addr} {USB_COMMAND 0xe0002140 USB_STATUS 0xe0002144 USB_PORT 0xe0002184 USB_MODE 0xe00021a8} {
    puts "$field: [mrd $addr]"
}
disconnect
exit
