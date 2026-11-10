#ifndef NAVIGATOR_AUDIO_H
#define NAVIGATOR_AUDIO_H
#include "xil_types.h"
#define AUDIO_RATE 48000U
#define AUDIO_RING_FRAMES 4096U
#define AUDIO_TARGET_FRAMES 1024U
/* Read over JTAG using scripts/status.tcl. Keep these fields in order. */
typedef struct {
    u32 magic, stage, codec_ok, configured, alternate;
    u32 usb_resets, setup_requests, packets, received_frames, played_frames;
    u32 underruns, overruns, malformed, ring_frames, feedback_q14;
    u32 usb_errors, last_setup, last_value_index, loops, nonzero_frames;
    u32 feedback_packets, i2s_errors;
    u32 feedback_result, feedback_queued;
} AudioDiagnostics;
extern volatile AudioDiagnostics diagnostics;
void audio_init(void);
void audio_stream(int enabled);
void audio_receive(const u8 *data, u32 bytes);
void audio_service(void);
u32 audio_feedback(void);
void audio_controls(u8 mute, s16 volume);
int usb_init(void);
void usb_poll(void);
#endif
