import sys,pefile,struct,json,bisect
from capstone import *
pe=pefile.PE(sys.argv[1],fast_load=True)
d=open(sys.argv[1],'rb').read()
IB=0x180000000
secs={s.Name.rstrip(b'\0').decode():s for s in pe.sections}
pd=secs['.pdata']; po=pd.PointerToRawData
funcs={}
for i in range(0,pd.Misc_VirtualSize,12):
    b,e,u=struct.unpack_from('<III',d,po+i)
    if b: funcs[b]=e
x=json.load(open(sys.argv[2]))
strs={a:s for a,f,s in x}
def rva2off(r):
    for s in pe.sections:
        if s.VirtualAddress<=r<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            return r-s.VirtualAddress+s.PointerToRawData
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=False
for a in sys.argv[3:]:
    f=int(a,16)-IB
    # find containing pdata function (chained entries: take the block given)
    e=funcs.get(f)
    if e is None:
        print('no pdata for',a); continue
    o=rva2off(f)
    print(f'===== {a} size {e-f:#x}')
    for ins in md.disasm(d[o:o+(e-f)], IB+f):
        line=f'{ins.address:x}: {ins.mnemonic} {ins.op_str}'
        r=ins.address-IB
        if r in strs: line+=f'    ; "{strs[r][:80]}"'
        print(line)
