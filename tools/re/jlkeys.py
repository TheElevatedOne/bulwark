import sys
d=open(sys.argv[1],'rb').read()
bs=int(sys.argv[2]) if len(sys.argv)>2 else 32
def ks(key,n):
    out=bytearray()
    for _ in range(n):
        out.append(key&0xff)
        key=((key<<1)^(0x1021 if key&0x8000 else 0))&0xffff
    return out
# precompute keystreams for all keys
streams=[ks(k,bs) for k in range(65536)]
def best(block):
    bk,bz=None,-1
    for k in range(65536):
        s=streams[k]
        z=sum(1 for a,b in zip(block,s) if a==b)
        if z>bz: bk,bz=k,z
    return bk,bz
for i in range(int(sys.argv[3]) if len(sys.argv)>3 else 12):
    blk=d[i*bs:(i+1)*bs]
    k,z=best(blk)
    dec=bytes(a^b for a,b in zip(blk,streams[k]))
    print(hex(i*bs), hex(k), z, dec.hex())
