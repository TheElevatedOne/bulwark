import sys,re
src=open(sys.argv[1]).read().split('\n')
starts=[]
pat=re.compile(r'^  ("?)([0-9a-f]{4})\1: function')
for i,l in enumerate(src):
    m=pat.match(l)
    if m: starts.append((i,m.group(2)))
want=set(sys.argv[3:])
import os
for k,(i,mid) in enumerate(starts):
    if mid in want:
        j=starts[k+1][0] if k+1<len(starts) else len(src)
        open(os.path.join(sys.argv[2],mid+'.js'),'w').write('\n'.join(src[i:j]))
        print(mid,i+1,j)
