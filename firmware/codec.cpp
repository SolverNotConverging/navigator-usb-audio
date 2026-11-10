#include "xparameters.h"
#include "xil_io.h"
#include "xiic_l.h"
#ifdef SDT
#include "xiltimer.h"
#else
#include "xtime_l.h"
#endif
#include "sleep.h"
#define IIC 0x41600000U
static u32 rd(u32 a,u32 o){return Xil_In32(a+o);}
static void wr(u32 a,u32 o,u32 v){Xil_Out32(a+o,v);}
static u32 ms(void){XTime t;XTime_GetTime(&t);return (u32)(t/(COUNTS_PER_SECOND/1000));}
static int iic_idle(void) {
    u32 t=ms();
    while(rd(IIC,XIIC_SR_REG_OFFSET)&XIIC_SR_BUS_BUSY_MASK) if(ms()-t>30) return 0;
    return 1;
}
static int codec_write(u8 reg, u8 value) {
    if(!iic_idle()) return 0;
    wr(IIC,XIIC_IISR_OFFSET,rd(IIC,XIIC_IISR_OFFSET));
    wr(IIC,XIIC_DTR_REG_OFFSET,0x100 | (0x10<<1));
    wr(IIC,XIIC_DTR_REG_OFFSET,reg);
    wr(IIC,XIIC_DTR_REG_OFFSET,0x200 | value);
    u32 t=ms();
    for(;;) {
        u32 s=rd(IIC,XIIC_SR_REG_OFFSET), e=rd(IIC,XIIC_IISR_OFFSET);
        if(e & (XIIC_INTR_TX_ERROR_MASK | XIIC_INTR_ARB_LOST_MASK)) return 0;
        if((s&XIIC_SR_TX_FIFO_EMPTY_MASK) && !(s&XIIC_SR_BUS_BUSY_MASK)) return 1;
        if(ms()-t>30) return 0;
    }
}
static int codec_read(u8 reg, u8 *value) {
    if(!iic_idle()) return 0;
    wr(IIC,XIIC_IISR_OFFSET,rd(IIC,XIIC_IISR_OFFSET));
    wr(IIC,XIIC_DTR_REG_OFFSET,0x100 | (0x10<<1));
    wr(IIC,XIIC_DTR_REG_OFFSET,reg);
    wr(IIC,XIIC_DTR_REG_OFFSET,0x100 | (0x10<<1) | 1);
    wr(IIC,XIIC_DTR_REG_OFFSET,0x200 | 1);
    u32 t=ms();
    while(rd(IIC,XIIC_SR_REG_OFFSET)&XIIC_SR_RX_FIFO_EMPTY_MASK) if(ms()-t>30) return 0;
    *value=(u8)rd(IIC,XIIC_DRR_REG_OFFSET);
    return iic_idle();
}
int init_codec(void) {
    if(XIic_DynInit(IIC)!=XST_SUCCESS) return 0;
    /* ES8388 slave, shared LRCK pin, 16-bit I2S, MCLK/Fs=256.
     * Headphone LOUT1/ROUT1 are enabled; onboard speaker LOUT2 stays off.
     * Analog outputs start at -15 dB; host additionally defaults to 25%.
     */
    const u8 settings[][2]={
        {0,0x80},{0,0x00},{25,0x04},{1,0x50},{2,0xf3},
        {3,0xff},{4,0xc0},{0,0x12},{8,0x00},{23,0x18},
        {24,0x02},{26,0x00},{27,0x00},{38,0x00},{39,0x90},
        {42,0x90},{43,0x80},{45,0x00},{46,0x14},{47,0x14},
        {48,0x14},{49,0x14},{2,0x00},{4,0x30}
    };
    for(unsigned i=0;i<sizeof(settings)/sizeof(settings[0]);i++) {
        if(!codec_write(settings[i][0],settings[i][1])) return 0;
        if(i<2) usleep(10000);
    }
    usleep(50000);
    u8 v=0;
    if(!codec_read(23,&v) || v!=0x18) return 0;
    if(!codec_read(24,&v) || v!=0x02) return 0;
    if(!codec_read(43,&v) || v!=0x80) return 0;
    return codec_write(25,0x00);
}
