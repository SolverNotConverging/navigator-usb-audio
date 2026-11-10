"""Check actual C++ descriptor bytes and UAC1 endpoint relationships."""
from pathlib import Path
import re
import unittest

def read_array(name):
    text=(Path(__file__).resolve().parents[1]/"firmware/descriptors.h").read_text()
    body=text.split(name+"[] = {")[1].split("};")[0]
    body=re.sub(r"/\*.*?\*/","",body,flags=re.S)
    return bytes(int(x.strip(),0) for x in body.split(",") if x.strip())

class Descriptors(unittest.TestCase):
    def test_configuration_lengths_and_audio_topology(self):
        data=read_array("configuration_descriptor")
        self.assertEqual(int.from_bytes(data[2:4],"little"),len(data))
        blocks=[]; offset=0
        while offset<len(data):
            length=data[offset]
            self.assertGreaterEqual(length,2)
            self.assertLessEqual(offset+length,len(data))
            blocks.append(data[offset:offset+length]);offset+=length
        ac=blocks[2:6]
        self.assertEqual(int.from_bytes(ac[0][5:7],"little"),sum(map(len,ac)))
        self.assertEqual(ac[2][4],ac[1][3]) # feature sources USB terminal
        self.assertEqual(ac[3][7],ac[2][3]) # speaker sources feature
        interfaces=[b for b in blocks if b[1]==4]
        self.assertEqual([(b[2],b[3],b[4]) for b in interfaces],[(0,0,0),(1,0,0),(1,1,2)])
        endpoint=[b for b in blocks if b[1]==5]
        self.assertEqual(endpoint[0][2:4],bytes([1,5]))
        self.assertEqual(endpoint[0][8],endpoint[1][2])
        self.assertEqual(endpoint[0][2]&15,endpoint[1][2]&15)
        self.assertEqual(endpoint[1][3],0x01) # UAC1 synch endpoint
        self.assertEqual(int.from_bytes(endpoint[1][4:6],"little"),3)
        fmt=next(b for b in blocks if len(b)==11 and b[1:3]==bytes([0x24,2]))
        rate=int.from_bytes(fmt[8:11],"little")
        self.assertEqual((fmt[4],fmt[5],fmt[6],rate),(2,2,16,48000))
        self.assertGreaterEqual(int.from_bytes(endpoint[0][4:6],"little"),(rate//1000+1)*4)
    def test_device(self):
        d=read_array("device_descriptor")
        self.assertEqual(len(d),18)
        self.assertEqual(d[7],64)
        self.assertEqual(d[-1],1)

if __name__=="__main__":unittest.main()
