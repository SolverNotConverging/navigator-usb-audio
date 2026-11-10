"""Windows-only playback check. Targets Navigator explicitly; no default change.

python scripts/test_playback.py --seconds 5
python scripts/test_playback.py --seconds 30 --silence
"""
import argparse
import ctypes as c
import math
import struct
import time

class Caps(c.Structure):
    _fields_ = [("mid",c.c_ushort),("pid",c.c_ushort),("version",c.c_uint32),
                ("name",c.c_wchar*32),("formats",c.c_uint32),
                ("channels",c.c_ushort),("reserved",c.c_ushort),("support",c.c_uint32)]
class Format(c.Structure):
    _fields_ = [("tag",c.c_ushort),("channels",c.c_ushort),("rate",c.c_uint32),
                ("bytes_sec",c.c_uint32),("align",c.c_ushort),("bits",c.c_ushort),("extra",c.c_ushort)]
class Header(c.Structure):
    _fields_ = [("data",c.c_void_p),("length",c.c_uint32),("recorded",c.c_uint32),
                ("user",c.c_size_t),("flags",c.c_uint32),("loops",c.c_uint32),
                ("next",c.c_void_p),("reserved",c.c_size_t)]

def check(result, operation):
    if result: raise RuntimeError(f"{operation}: WinMM error {result}")

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--seconds",type=int,default=5)
    parser.add_argument("--silence",action="store_true")
    args=parser.parse_args()
    if not 1<=args.seconds<=60: parser.error("seconds must be between 1 and 60")
    api=c.WinDLL("winmm")
    api.waveOutGetDevCapsW.argtypes=[c.c_size_t,c.POINTER(Caps),c.c_uint]
    api.waveOutOpen.argtypes=[c.POINTER(c.c_void_p),c.c_uint,c.POINTER(Format),c.c_size_t,c.c_size_t,c.c_uint]
    for name in ["waveOutPrepareHeader","waveOutWrite","waveOutUnprepareHeader"]:
        getattr(api,name).argtypes=[c.c_void_p,c.POINTER(Header),c.c_uint]
    api.waveOutReset.argtypes=[c.c_void_p]
    api.waveOutClose.argtypes=[c.c_void_p]
    rate=48000; count=rate*args.seconds
    pcm=bytearray(count*4)
    if not args.silence:
        for i in range(count):
            fade=min(1,i/(rate*.05),(count-1-i)/(rate*.05))
            left=int(1024*fade*math.sin(2*math.pi*440*i/rate))
            right=int(1024*fade*math.sin(2*math.pi*660*i/rate))
            struct.pack_into("<hh",pcm,i*4,left,right)
    data=c.create_string_buffer(bytes(pcm))
    header=Header(c.cast(data,c.c_void_p),len(pcm),0,0,0,0,None,0)
    fmt=Format(1,2,rate,rate*4,4,16,0); handle=c.c_void_p()
    # After JTAG reload, Windows can briefly list the previous endpoint before
    # its replacement has a ready driver. Re-enumerate and open as one operation.
    deadline=time.monotonic()+15
    opened=False
    last_error="Navigator USB Audio output was not found"
    while not opened and time.monotonic()<deadline:
        for device in range(api.waveOutGetNumDevs()):
            caps=Caps()
            if api.waveOutGetDevCapsW(device,c.byref(caps),c.sizeof(caps)): continue
            if "Navigator USB Audio" not in caps.name: continue
            result=api.waveOutOpen(c.byref(handle),device,c.byref(fmt),0,0,0)
            if result==0:
                opened=True; print(caps.name,flush=True); break
            last_error=f"open 48 kHz stereo: WinMM error {result}"
        if not opened: time.sleep(.25)
    if not opened: raise RuntimeError(last_error)
    prepared=False
    try:
        check(api.waveOutPrepareHeader(handle,c.byref(header),c.sizeof(header)),"prepare")
        prepared=True
        check(api.waveOutWrite(handle,c.byref(header),c.sizeof(header)),"write")
        print(f"Streaming {args.seconds} seconds, 48000 Hz / 16-bit / stereo",flush=True)
        deadline=time.monotonic()+args.seconds+10
        while not header.flags&1:
            if time.monotonic()>deadline: raise TimeoutError("Audio playback did not finish")
            time.sleep(.05)
        print("Windows playback completed",flush=True)
    finally:
        api.waveOutReset(handle)
        if prepared: api.waveOutUnprepareHeader(handle,c.byref(header),c.sizeof(header))
        api.waveOutClose(handle)

if __name__=="__main__": main()
