import sys,re,struct,pefile,bisect,json
from capstone import *
pe=pefile.PE(sys.argv[1],fast_load=True); d=open(sys.argv[1],'rb').read()
IB=0x180000000
t=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
tb=IB+t.VirtualAddress
code=d[t.PointerToRawData:t.PointerToRawData+t.Misc_VirtualSize]
secs={s.Name.rstrip(b'\0').decode():s for s in pe.sections}
pd=secs['.pdata']; po=pd.PointerToRawData
fs=[]
for i in range(0,pd.Misc_VirtualSize,12):
    b,e,u=struct.unpack_from('<III',d,po+i)
    if b: fs.append((b+IB,e+IB))
fs.sort(); st=[f[0] for f in fs]
strs={a+IB:s for a,f,s in json.load(open(sys.argv[2]))}
tgt=int(sys.argv[3],16); n=int(sys.argv[4]) if len(sys.argv)>4 else 12
md=Cs(CS_ARCH_X86,CS_MODE_64)
for m in re.finditer(rb'\xe8',code):
    i=m.start(); rel,=struct.unpack_from('<i',code,i+1); a=tb+i
    if a+5+rel!=tgt: continue
    k=bisect.bisect_right(st,a)-1; fb,fe=fs[k]
    o=pe.get_offset_from_rva(fb-IB)
    ins=[x for x in md.disasm(d[o:o+(a-fb)+5],fb)]
    tail=[x for x in ins if x.address<=a][-n:]
    sref=[strs[x.address][:40] for x in ins if x.address in strs][-3:]
    print(f'--- {a:x} in {fb:x} strs={sref}')
    for x in tail: print(f'   {x.address:x}: {x.mnemonic} {x.op_str}')
