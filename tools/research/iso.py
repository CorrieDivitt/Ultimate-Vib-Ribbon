import sys, struct, os
binp, outdir = sys.argv[1], sys.argv[2]
f = open(binp,'rb')
SEC=2352
def sector(lba):
    f.seek(lba*SEC); raw=f.read(SEC)
    mode=raw[15]
    if mode==2:
        sub=raw[18]
        if sub & 0x20: return raw[24:24+2324], 2
        return raw[24:24+2048], 1
    return raw[16:16+2048], 1
def read(lba,size):
    out=b''
    n=(size+2047)//2048
    for i in range(n):
        d,form=sector(lba+i); out+=d[:2048]
    return out[:size]
def read_raw_form(lba,size):
    # returns (data, has_form2)
    n=(size+2047)//2048; out=b''; f2=False
    for i in range(n):
        d,form=sector(lba+i)
        if form==2: f2=True
        out+=d
    return out, f2
pvd=read(16,2048)
print("SYSID",pvd[8:40].decode('ascii','replace').strip(),"VOLID",pvd[40:72].decode('ascii','replace').strip())
print("volume blocks",struct.unpack('<I',pvd[80:84])[0], "created",pvd[813:830])
root=pvd[156:190]
def walk(lba,size,path):
    data=read(lba,size); i=0
    while i<len(data):
        l=data[i]
        if l==0:
            i=(i//2048+1)*2048; continue
        rec=data[i:i+l]
        elba=struct.unpack('<I',rec[2:6])[0]; esize=struct.unpack('<I',rec[10:14])[0]
        flags=rec[25]; nl=rec[32]; name=rec[33:33+nl]
        i+=l
        if name in (b'\x00',b'\x01'): continue
        name=name.decode('ascii').split(';')[0]
        p=os.path.join(path,name)
        if flags&2:
            os.makedirs(os.path.join(outdir,p),exist_ok=True)
            print("DIR  %8s lba=%6d %s"%('',elba,p)); walk(elba,esize,p)
        else:
            d,f2=read_raw_form(elba,esize)
            if not f2: d=d[:esize]
            print("FILE %8d lba=%6d %s%s"%(esize,elba,p,' [XA form2]' if f2 else ''))
            open(os.path.join(outdir,p),'wb').write(d)
os.makedirs(outdir,exist_ok=True)
walk(struct.unpack('<I',root[2:6])[0],struct.unpack('<I',root[10:14])[0],'')
