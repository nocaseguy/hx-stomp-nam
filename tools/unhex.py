#!/usr/bin/env python3
"""unhex.py - reassemble an Intel-HEX flash dump (from flash_dump_ihex over UART)
back into a raw binary. Usage: unhex.py dump.hex out.bin"""
import sys
def main(inp, outp):
    base=0; data={}
    for line in open(inp):
        line=line.strip()
        if not line.startswith(':'): continue
        b=bytes.fromhex(line[1:])
        n,addr,typ = b[0], (b[1]<<8)|b[2], b[3]
        if (sum(b)&0xFF)!=0: raise SystemExit(f"checksum error: {line}")
        if typ==0x04: base=((b[4]<<8)|b[5])<<16
        elif typ==0x00:
            for i in range(n): data[base+addr+i]=b[4+i]
        elif typ==0x01: break
    if not data: raise SystemExit("no data")
    lo,hi=min(data),max(data)
    out=bytearray(b'\xff'*(hi-lo+1))
    for a,v in data.items(): out[a-lo]=v
    open(outp,'wb').write(out)
    print(f"[+] {outp}: {len(out)} bytes, flash offset {lo:#x}..{hi:#x}")
if __name__=='__main__':
    main(sys.argv[1], sys.argv[2])
