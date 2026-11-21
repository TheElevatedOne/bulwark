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
targets=[int(x,16) for x in sys.argv[3:]]
md=Cs(CS_ARCH_X86,CS_MODE_64)
for m in re.finditer(rb'\xe8',code):
    i=m.start()
    rel,=struct.unpack_from('<i',code,i+1)
    a=tb+i; tgt=a+5+rel
    if tgt not in targets: continue
    k=bisect.bisect_right(st,a)-1; fb,fe=fs[k]
    # disassemble function from start up to call
    o=pe.get_offset_from_rva(fb-IB)
    ins=[x for x in md.disasm(d[o:o+(a-fb)+5],fb)]
    # last 45 instructions before call
    tail=[x for x in ins if x.address<a][-45:]
    ecx=None; bytes0=[]
    for x in tail:
        if x.mnemonic=='mov' and x.op_str.startswith(('ecx, ','cl, ')):
            ecx=x.op_str.split(', ')[1]
        mm=re.match(r'byte ptr \[r.. \+ 0x1([0-9a-f])\], (0x[0-9a-f]+|\d+)$',x.op_str)
        if x.mnemonic=='mov' and mm: bytes0.append((int(mm.group(1),16),mm.group(2)))
    sref=[strs[x.address] for x in ins if x.address in strs][:3]
    print(f'{a:x} in {fb:x} -> {tgt:x} msgId={ecx} payload={sorted(set(bytes0))[:8]} strs={[s[:40] for s in sref]}')
