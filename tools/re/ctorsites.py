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
tgt=int(sys.argv[3],16)
md=Cs(CS_ARCH_X86,CS_MODE_64)
for m in re.finditer(rb'\xe8',code):
    i=m.start(); rel,=struct.unpack_from('<i',code,i+1); a=tb+i
    if a+5+rel!=tgt: continue
    k=bisect.bisect_right(st,a)-1; fb,fe=fs[k]
    o=pe.get_offset_from_rva(fb-IB)
    ins=[x for x in md.disasm(d[o:o+(a-fb)+5],fb)]
    tail=[x for x in ins if x.address<a][-14:]
    regs={}
    for x in tail:
        mm=re.match(r'(edx|r8d|r9d|r8|r9|edx), (0x[0-9a-f]+|\d+)$',x.op_str)
        if x.mnemonic=='mov' and mm: regs[mm.group(1)]=int(mm.group(2),0)
        mm=re.match(r'(r8d|r9d), \[(rdx|r8|r9) \+ (0x[0-9a-f]+)\]$',x.op_str)
        if x.mnemonic=='lea' and mm:
            src={'rdx':'edx','r8':'r8d','r9':'r9d'}[mm.group(2)]
            if src in regs: regs[mm.group(1)]=regs[src]+int(mm.group(3),16)
        mm=re.match(r'dword ptr \[rsp \+ 0x20\], (0x[0-9a-f]+|\d+)$',x.op_str)
        if x.mnemonic=='mov' and mm: regs['len']=int(mm.group(1),0)
        if x.mnemonic=='xor' and x.op_str in ('edx, edx','r8d, r8d','r9d, r9d'): regs[x.op_str.split(',')[0]]=0
    sref=[strs[x.address][:30] for x in ins if x.address in strs][-3:]
    f=lambda k: hex(regs[k]) if k in regs else '?'
    print(f'{a:x} in {fb:x}: cmd={f("edx")} sub={f("r8d")} addr={f("r9d")} len={f("len")} strs={sref}')
