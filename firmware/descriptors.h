#ifndef NAVIGATOR_DESCRIPTORS_H
#define NAVIGATOR_DESCRIPTORS_H
#include "xil_types.h"
/* Development-only VID/PID from the AMD USB example. Not a production ID. */
static const u8 device_descriptor[] = {
    18,1,0x00,0x02,0,0,0,64, 0xfd,0x03,0x00,0x02,0x01,0x01,1,2,3,1
};
static const u8 configuration_descriptor[] = {
    9,2,119,0,2,1,0,0xc0,1,             /* self-powered */
    9,4,0,0,0,1,1,0,0,                 /* AudioControl */
    9,0x24,1,0,1,40,0,1,1,
    12,0x24,2,1,0x01,0x01,0,2,3,0,0,0, /* USB streaming input terminal */
    10,0x24,6,2,1,1,3,0,0,0,           /* master mute and volume */
    9,0x24,3,3,0x01,0x03,0,2,0,        /* speaker output terminal */
    9,4,1,0,0,1,2,0,0,                 /* zero-bandwidth alternate */
    9,4,1,1,2,1,2,0,0,
    7,0x24,1,1,1,1,0,                  /* PCM linked to terminal 1 */
    11,0x24,2,1,2,2,16,1,0x80,0xbb,0,  /* 48 kHz, stereo, signed 16 bit */
    9,5,0x01,0x05,196,0,1,0,0x81,      /* asynchronous OUT + feedback */
    7,0x25,1,0,0,0,0,                  /* fixed sampling frequency */
    9,5,0x81,0x01,3,0,1,4,0            /* UAC1 synch endpoint; 10.14 feedback */
};
static_assert(sizeof(configuration_descriptor)==119, "USB configuration length mismatch");
#endif
