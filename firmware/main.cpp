#include "audio.h"
#include "xil_cache.h"
int main(void) {
    /* USB DMA and application share OCM. No cacheable DMA buffers. */
    Xil_DCacheDisable();
    Xil_ICacheEnable();
    diagnostics.magic=0x55414331;
    diagnostics.stage=1;
    audio_init();
    diagnostics.stage=2;
    if (!usb_init()) { diagnostics.stage=0xe001; for(;;) {} }
    diagnostics.stage=3;
    for (;;) { usb_poll(); audio_service(); diagnostics.loops++; }
}
