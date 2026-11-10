#include "audio.h"
#include "xil_io.h"
#include "sleep.h"
#include <math.h>

#define FIFO 0x43C00000U
#define I2S 0x43C10000U
static u32 samples[AUDIO_RING_FRAMES];
static u32 head, tail, used, frame;
static int enabled, playing;
static s32 gain = 8231; /* -12 dB digital gain, in addition to codec attenuation. */
extern int init_codec(void);
volatile AudioDiagnostics diagnostics{};

void audio_controls(u8 mute, s16 volume) {
    gain = mute ? 0 : (s32)(32767.0f * powf(10.0f, volume / 5120.0f));
}
void audio_stream(int on) {
    enabled = on; playing = 0; head = tail = used = 0;
    diagnostics.alternate = on; diagnostics.ring_frames = 0;
}
void audio_receive(const u8 *data, u32 bytes) {
    diagnostics.packets++;
    if ((bytes & 3U) || bytes > 196) { diagnostics.malformed++; return; }
    if (!enabled) return;
    for (u32 n = 0; n < bytes; n += 4) {
        if (used == AUDIO_RING_FRAMES) { diagnostics.overruns++; break; }
        u32 v = data[n] | ((u32)data[n+1] << 8) |
                ((u32)data[n+2] << 16) | ((u32)data[n+3] << 24);
        samples[head] = v; head = (head+1) & (AUDIO_RING_FRAMES-1); used++;
        diagnostics.received_frames++;
        if (v) diagnostics.nonzero_frames++;
    }
    if (!playing && used >= AUDIO_TARGET_FRAMES) playing = 1;
    diagnostics.ring_frames = used;
}
void audio_service(void) {
    /* One bounded batch per USB poll; each frame has left/right AXIS words. */
    if (Xil_In32(FIFO+0x0c) < 64) return;
    for (unsigned i=0; i<32; ++i) {
        s16 left=0, right=0;
        if (enabled && playing) {
            if (used) {
                u32 v=samples[tail]; tail=(tail+1)&(AUDIO_RING_FRAMES-1); used--;
                left=(s16)(((s32)(s16)v * gain) / 32768);
                right=(s16)(((s32)(s16)(v>>16) * gain) / 32768);
                diagnostics.played_frames++;
            } else { diagnostics.underruns++; playing=0; }
        }
        Xil_Out32(FIFO+0x10, ((u32)(u16)left<<12) | (frame==0?1:2));
        Xil_Out32(FIFO+0x10, 0x20000000U | ((u32)(u16)right<<12) | 3);
        frame=(frame+1)%192;
    }
    Xil_Out32(FIFO+0x14, 64*4);
    diagnostics.ring_frames=used;
    u32 status=Xil_In32(I2S+0x14);
    if (status) {
        if(enabled && playing && (status&8)) diagnostics.i2s_errors++;
        Xil_Out32(I2S+0x14,status);
    }
}
u32 audio_feedback(void) {
    /* FS asynchronous feedback in samples/ms, 10.14 format. A slow buffer
       servo follows oscillator drift without dropping/repeating samples.
       The 32-frame service batches cause only a tiny feedback modulation. */
    s32 error=(s32)AUDIO_TARGET_FRAMES-(s32)used;
    s32 correction=playing ? error*2 : 0;
    if(correction>819) correction=819;
    if(correction< -819) correction= -819;
    return (48U<<14)+correction;
}
void audio_init(void) {
    Xil_Out32(FIFO+0x28,0xa5); usleep(1000); Xil_Out32(FIFO+4,0);
    Xil_Out32(I2S+8,0); Xil_Out32(I2S+0x10,0);
    Xil_Out32(I2S+0x30,1); Xil_Out32(I2S+0x20,2);
    for(int i=0;i<7;i++) audio_service();
    Xil_Out32(I2S+8,1);
    diagnostics.codec_ok=init_codec();
}
