/* USB Audio Class 1 asynchronous stereo sink. USB0 is deliberately forced to
 * full speed: 48 kHz stereo needs 192 bytes/ms, below the 1023-byte FS limit.
 * Endpoint callbacks run from usb_poll(), not interrupt context. */
#include "audio.h"
#include "descriptors.h"
#include "xusbps.h"
#include "xusbps_endpoint.h"
#include "xil_io.h"
#include "sleep.h"
#ifdef SDT
#include "xiltimer.h"
#else
#include "xtime_l.h"
#endif
#include <string.h>
#include <initializer_list>

#define USB 0xe0002000U
#define IN XUSBPS_EP_DIRECTION_IN
#define OUT XUSBPS_EP_DIRECTION_OUT
static XUsbPs controller;
static XUsbPs_DeviceConfig config;
static u8 dma_memory[32768] __attribute__((aligned(2048)));
static u8 rx_packet[256] __attribute__((aligned(32)));
static u8 reply[256] __attribute__((aligned(32)));
static u8 feedback[32] __attribute__((aligned(32)));
static u8 configured, alternate, pending_control, muted;
static s16 volume=-12*256;
static int feedback_pending;
static u32 last_packet_ms;

static u32 millis(void) { XTime t; XTime_GetTime(&t); return (u32)(t/(COUNTS_PER_SECOND/1000)); }
static void stall(void) { XUsbPs_EpStall(&controller,0,IN|OUT); }
static void send_reply(const void *data, u32 count, u32 requested) {
    if(count>requested) count=requested;
    if(count) memcpy(reply,data,count);
    if(XUsbPs_EpBufferSend(&controller,0,count?reply:NULL,count)!=XST_SUCCESS)
        diagnostics.usb_errors++;
}
static void ack(void) { send_reply(NULL,0,0); }
static void feedback_done(void *ref,u32 requested,u32 sent) {
    (void)ref;(void)requested;(void)sent; feedback_pending=0;
    diagnostics.feedback_packets++;
}
static void receive_done(void *ref,u32 requested,u32 received) {
    (void)ref;(void)requested;
    if(received<=sizeof(rx_packet)) audio_receive(rx_packet,received);
    else diagnostics.malformed++;
    last_packet_ms=millis();
    /* Re-arm without recursive driver calls if further packets are pending. */
    XUsbPs_EpOut *ep=&controller.DeviceConfig.Ep[1].Out;
    ep->BufferPtr=rx_packet; ep->RequestedBytes=196; ep->BytesTxed=0; ep->MemAlloted=1;
}
static void queue_feedback(void) {
    if(feedback_pending) return;
    u32 f=audio_feedback(); diagnostics.feedback_q14=f;
    feedback[0]=f;feedback[1]=f>>8;feedback[2]=f>>16;
    int result=XUsbPs_EpBufferSend(&controller,1,feedback,3);
    diagnostics.feedback_result=result;
    if(result==XST_SUCCESS) {feedback_pending=1; diagnostics.feedback_queued++;}
}
static void stream_setting(u8 alt) {
    XUsbPs_EpDisable(&controller,1,OUT);
    XUsbPs_EpDisable(&controller,1,IN);
    XUsbPs_EpFlush(&controller,1,OUT);
    XUsbPs_EpFlush(&controller,1,IN);
    alternate=alt; feedback_pending=0; audio_stream(alt);
    if(!alt) return;
    u32 started=millis();
    while(Xil_In32(USB+XUSBPS_EPFLUSH_OFFSET)&0x00020002U) {
        if(millis()-started>10) {diagnostics.usb_errors++;return;}
    }
    /* A cancelled ISO transfer may still have an active software descriptor.
       Rebuild both lists on every alternate-setting transition. */
    memset(controller.DeviceConfig.Ep[1].Out.dQH,0,64);
    memset(controller.DeviceConfig.Ep[1].In.dQH,0,64);
    memset(controller.DeviceConfig.Ep[1].Out.dTDs,0,16*32);
    memset(controller.DeviceConfig.Ep[1].In.dTDs,0,4*32);
    XUsbPs_ReconfigureEp(&controller,&controller.DeviceConfig,1,OUT,0);
    XUsbPs_ReconfigureEp(&controller,&controller.DeviceConfig,1,IN,0);
    /* UG585: ISO queue heads require Mult=1 for one transaction per frame.
       usbps 2.10 derives zero from packet sizes below 1024; correct that here. */
    for(auto qh : {controller.DeviceConfig.Ep[1].Out.dQH,controller.DeviceConfig.Ep[1].In.dQH}) {
        XUsbPs_WritedQH(qh,XUSBPS_dQHCFG,XUsbPs_ReaddQH(qh,XUSBPS_dQHCFG)|(1U<<30));
        XUsbPs_dQHFlushCache(qh);
    }
    /* UAC1 feedback shares the OUT endpoint number, with opposite direction.
       ISO type in hardware is 1. No data toggles on ISO endpoints. */
    Xil_Out32(USB+XUSBPS_EPCRn_OFFSET(1),XUSBPS_EPCR_RXE_MASK | (1U<<XUSBPS_EPCR_RXT_TYPE_SHIFT) | XUSBPS_EPCR_TXE_MASK | (1U<<XUSBPS_EPCR_TXT_TYPE_SHIFT));
    XUsbPs_EpOut *ep=&controller.DeviceConfig.Ep[1].Out;
    ep->BufferPtr=rx_packet; ep->RequestedBytes=196; ep->BytesTxed=0; ep->MemAlloted=1;
    XUsbPs_EpPrime(&controller,1,OUT);
    last_packet_ms=millis();
    /* Be ready for the first host feedback poll before acknowledging SET_INTERFACE. */
    queue_feedback();
}
static void string_descriptor(u8 index,u16 length) {
    const char *s;
    static const u8 language[]={4,3,9,4};
    if(index==0) { send_reply(language,4,length); return; }
    if(index==1) s="Navigator Lab";
    else if(index==2) s="Navigator USB Audio";
    else if(index==3) s="NAV7020-AUDIO-01";
    else { stall(); return; }
    u8 data[128]={0,3}; u32 n=(u32)strlen(s);
    data[0]=(u8)(2+2*n);
    for(u32 i=0;i<n;i++) data[2+2*i]=(u8)s[i];
    send_reply(data,data[0],length);
}
static void setup_request(XUsbPs_SetupData *s) {
    pending_control=0;
    diagnostics.setup_requests++;
    diagnostics.last_setup=(s->bmRequestType<<24)|(s->bRequest<<16)|s->wLength;
    diagnostics.last_value_index=(s->wValue<<16)|s->wIndex;
    if((s->bmRequestType&0x60)==0) {
        switch(s->bRequest) {
        case 6:
            if(s->bmRequestType!=0x80) break;
            switch(s->wValue>>8) {
            case 1: send_reply(device_descriptor,sizeof(device_descriptor),s->wLength); return;
            case 2: send_reply(configuration_descriptor,sizeof(configuration_descriptor),s->wLength); return;
            case 3: string_descriptor((u8)s->wValue,s->wLength); return;
            default: break; /* full-speed-only device: no device qualifier */
            }
            break;
        case 5:
            if(s->bmRequestType || s->wValue>127 || s->wLength) break;
            XUsbPs_SetDeviceAddress(&controller,(u8)s->wValue); ack(); return;
        case 9:
            if(s->bmRequestType || s->wValue>1 || s->wLength) break;
            configured=(u8)s->wValue; diagnostics.configured=configured;
            stream_setting(0); ack(); return;
        case 8: if(s->bmRequestType==0x80) {send_reply(&configured,1,s->wLength);return;} break;
        case 11:
            if(s->bmRequestType!=1 || !configured || s->wLength) break;
            if(s->wIndex==0 && s->wValue==0) {ack();return;}
            if(s->wIndex==1 && s->wValue<=1) {stream_setting((u8)s->wValue);ack();return;}
            break;
        case 10:
            if(s->bmRequestType==0x81 && configured && s->wIndex<=1) {
                u8 a=s->wIndex?alternate:0; send_reply(&a,1,s->wLength);return;
            } break;
        case 0: {
            u16 status=s->bmRequestType==0x80?1:0; /* self-powered, no remote wake */
            if((s->bmRequestType&0x80) && s->wLength==2) {send_reply(&status,2,2);return;}
            break;
        }
        default: break;
        }
    } else if((s->bmRequestType==0xa1 || s->bmRequestType==0x21) &&
              s->wIndex==0x0200 && (s->wValue&255)==0) {
        u8 selector=s->wValue>>8;
        if(s->bmRequestType==0x21 && s->bRequest==1) {
            if((selector==1 && s->wLength==1) || (selector==2 && s->wLength==2)) {
                pending_control=selector; return; /* status after OUT data */
            }
        } else if(s->bmRequestType==0xa1) {
            if(selector==1 && s->bRequest==0x81) {send_reply(&muted,1,s->wLength);return;}
            if(selector==2) {
                s16 v;
                switch(s->bRequest) {
                case 0x81:v=volume;break;
                case 0x82:v=-60*256;break;
                case 0x83:v=0;break;
                case 0x84:v=256;break;
                default:stall();return;
                }
                send_reply(&v,2,s->wLength);return;
            }
        }
    }
    stall();
}
static void ep0_event(void *ref,u8 ep,u8 event,void *data) {
    (void)ref;(void)data;
    if(event==XUSBPS_EP_EVENT_SETUP_DATA_RECEIVED) {
        XUsbPs_SetupData setup;
        XUsbPs_EpUnStall(&controller,0,IN|OUT);
        if(XUsbPs_EpGetSetupData(&controller,0,&setup)==XST_SUCCESS) setup_request(&setup);
        else stall();
    } else if(event==XUSBPS_EP_EVENT_DATA_RX) {
        u8 *buffer; u32 len,handle;
        if(XUsbPs_EpBufferReceive(&controller,ep,&buffer,&len,&handle)==XST_SUCCESS) {
            int valid=0;
            if(pending_control==1 && len==1 && buffer[0]<=1) {muted=buffer[0];valid=1;}
            if(pending_control==2 && len==2) {
                s16 v=(s16)(buffer[0]|(buffer[1]<<8));
                if(v>=-60*256 && v<=0) {volume=v;valid=1;}
            }
            XUsbPs_EpBufferRelease(handle);
            if(pending_control) {
                if(valid) {audio_controls(muted,volume);ack();} else stall();
                pending_control=0;
            }
        }
    }
}
static void controller_event(void *ref,u32 mask) {
    (void)ref;
    if(mask&XUSBPS_IXR_UR_MASK) {
        diagnostics.usb_resets++; configured=alternate=pending_control=0;
        diagnostics.configured=0; audio_stream(0); feedback_pending=0;
    }
    if(mask&XUSBPS_IXR_UE_MASK) diagnostics.usb_errors++;
}
static int phy_init(void) {
    /* USB3320 RESETB is MIO9, GPIO bank 0. Never enable the host VBUS source. */
    Xil_Out32(0xf8000008,0xdf0d);
    Xil_Out32(0xf8000724,0x1600);
    Xil_Out32(0xf800012c,Xil_In32(0xf800012c)|(1U<<22));
    Xil_Out32(0xf8000004,0x767b);
    Xil_Out32(0xe000a204,Xil_In32(0xe000a204)|(1U<<9));
    Xil_Out32(0xe000a208,Xil_In32(0xe000a208)|(1U<<9));
    Xil_Out32(0xe000a000,0xfdff0000); usleep(10000);
    Xil_Out32(0xe000a000,0xfdff0200); usleep(50000);
    return 1;
}
int usb_init(void) {
    XUsbPs_Config cfg{}; cfg.BaseAddress=USB;
    phy_init();
    if(XUsbPs_CfgInitialize(&controller,&cfg,USB)!=XST_SUCCESS) return 0;
    config.NumEndpoints=2; config.DMAMemPhys=(u32)dma_memory;
    for(unsigned e=0;e<2;e++) {
        config.EpCfg[e].Out.Type=XUSBPS_EP_TYPE_CONTROL;
        config.EpCfg[e].Out.NumBufs=2; config.EpCfg[e].Out.BufSize=64;
        config.EpCfg[e].Out.MaxPacketSize=64;
        config.EpCfg[e].In.Type=XUSBPS_EP_TYPE_CONTROL;
        config.EpCfg[e].In.NumBufs=2; config.EpCfg[e].In.MaxPacketSize=64;
    }
    config.EpCfg[1].Out.Type=XUSBPS_EP_TYPE_ISOCHRONOUS;
    config.EpCfg[1].Out.NumBufs=16; config.EpCfg[1].Out.BufSize=196;
    config.EpCfg[1].Out.MaxPacketSize=196;
    config.EpCfg[1].In.Type=XUSBPS_EP_TYPE_ISOCHRONOUS;
    config.EpCfg[1].In.NumBufs=4; config.EpCfg[1].In.MaxPacketSize=3;
    if(XUsbPs_ConfigureDevice(&controller,&config)!=XST_SUCCESS) return 0;
    /* Select ULPI; force FS. Preserve status bits without writing W1C flags. */
    Xil_Out32(USB+0x184,(Xil_In32(USB+0x184)&~0xc100102aU)|0x81000000U);
    XUsbPs_EpSetHandler(&controller,0,OUT,ep0_event,&controller);
    XUsbPs_EpSetIsoHandler(&controller,1,OUT,receive_done);
    XUsbPs_EpSetIsoHandler(&controller,1,IN,feedback_done);
    XUsbPs_IntrSetHandler(&controller,controller_event,&controller,
                         XUSBPS_IXR_UR_MASK|XUSBPS_IXR_UE_MASK);
    XUsbPs_IntrEnable(&controller,XUSBPS_IXR_UR_MASK|XUSBPS_IXR_UI_MASK|XUSBPS_IXR_UE_MASK);
    XUsbPs_EpEnable(&controller,0,IN|OUT);
    XUsbPs_EpPrime(&controller,0,OUT);
    XUsbPs_Start(&controller);
    return 1;
}
void usb_poll(void) {
    if(Xil_In32(USB+XUSBPS_ISR_OFFSET)) XUsbPs_IntrHandler(&controller);
    if(alternate) queue_feedback();
    if(alternate && millis()-last_packet_ms>100) {
        /* Stop queued playback after a host disconnect/suspend. */
        audio_stream(0); audio_stream(1); last_packet_ms=millis();
    }
}
