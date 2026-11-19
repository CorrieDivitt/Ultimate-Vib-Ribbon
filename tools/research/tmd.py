import sys,struct,collections,glob,os
root=sys.argv[1]
tot=collections.Counter()
for fp in sorted(glob.glob(root+'/**/*.TMD',recursive=True)):
    d=open(fp,'rb').read()
    idv,flags,nobj=struct.unpack('<III',d[:12])
    modes=collections.Counter(); nv=0; npm=0
    for i in range(nobj):
        vt,nvert,nt,nn,pt,nprim,scale=struct.unpack('<IIIIIIi',d[12+i*28:12+i*28+28])
        nv+=nvert; npm+=nprim
        p=12+pt if not flags&1 else pt
        for k in range(nprim):
            olen,ilen,flag,mode=d[p],d[p+1],d[p+2],d[p+3]
            modes['%02X'%mode]+=1; p+=4+ilen*4
    tot.update(modes)
    print('%-34s id=%08X obj=%3d verts=%5d prims=%5d modes=%s size=%d'%(fp[len(root)+1:],idv,nobj,nv,npm,dict(modes),len(d)))
print('TOTAL modes',dict(tot))
