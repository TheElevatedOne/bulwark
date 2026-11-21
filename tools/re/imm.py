import sys,re,struct,pefile
from capstone import *
p=sys.argv[1]; val=int(sys.argv[2],16)
pe=pefile.PE(p,fast_load=True); d=open(p,'rb').read(); IB=0x180000000
t=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
code=d[t.PointerToRawData:t.PointerToRawData+t.Misc_VirtualSize]; tb=IB+t.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64)
pat=struct.pack('<I',val)
n=0
for m in re.finditer(re.escape(pat),code):
    i=m.start()
    for back in (1,2,3,4,6,7):
        s=i-back
        ins=list(md.disasm(code[s:s+16],tb+s,count=1))
        if ins and ins[0].size==back+4 and ('%#x'%val) in ins[0].op_str:
            # context
            ctx=list(md.disasm(code[s-40:s+40],tb+s-40))
            print(hex(tb+s),ins[0].mnemonic,ins[0].op_str); n+=1; break
print(n,'hits')
