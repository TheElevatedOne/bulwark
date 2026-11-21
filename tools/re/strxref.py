import sys,pefile,struct,re,bisect,json
pe=pefile.PE(sys.argv[1],fast_load=True)
d=open(sys.argv[1],'rb').read()
ib=pe.OPTIONAL_HEADER.ImageBase
secs={s.Name.rstrip(b'\0').decode():s for s in pe.sections}
text=secs['.text']; 
tva=text.VirtualAddress; traw=text.PointerToRawData; tsz=text.Misc_VirtualSize
def rva2off(r):
    for s in pe.sections:
        if s.VirtualAddress<=r<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            return r-s.VirtualAddress+s.PointerToRawData
    return None
STRMT=0x18003ef58
def getstr(rva):
    o=rva2off(rva)
    if o is None or o+12>len(d): return None
    mt,=struct.unpack_from('<Q',d,o)
    if mt!=STRMT: return None
    n,=struct.unpack_from('<I',d,o+8)
    if n>4000: return None
    try: return d[o+12:o+12+2*n].decode('utf-16le')
    except: return None
# pdata
pd=secs['.pdata']; po=pd.PointerToRawData; psz=pd.Misc_VirtualSize
funcs=[]
for i in range(0,psz,12):
    b,e,u=struct.unpack_from('<III',d,po+i)
    if b: funcs.append((b,e))
funcs.sort(); starts=[f[0] for f in funcs]
code=d[traw:traw+tsz]
out=[]
# lea reg,[rip+disp32]: REX.W(48/4C) 8D modrm(00 reg 101)
for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',code):
    i=m.start()
    disp,=struct.unpack_from('<i',code,i+3)
    insn_rva=tva+i
    tgt=insn_rva+7+disp
    s=getstr(tgt)
    if s is None: continue
    k=bisect.bisect_right(starts,insn_rva)-1
    f=funcs[k][0] if k>=0 and funcs[k][0]<=insn_rva<funcs[k][1] else None
    out.append((insn_rva,f,s))
json.dump(out,open(sys.argv[2],'w'))
print(len(out),'refs', len(funcs),'pdata funcs')
