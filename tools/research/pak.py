import sys,struct,os,collections
src,out=sys.argv[1],sys.argv[2]
d=open(src,'rb').read()
n=struct.unpack('<I',d[:4])[0]
offs=struct.unpack('<%dI'%n,d[4:4+4*n])
ext=collections.Counter(); sizes=collections.Counter()
for o in offs:
    e=d.index(b'\0',o); name=d[o:e].decode('ascii')
    p=o+((len(name)//4)+1)*4
    ln=struct.unpack('<I',d[p:p+4])[0]
    data=d[p+4:p+4+ln]
    x=name.rsplit('.',1)[-1]; ext[x]+=1; sizes[x]+=ln
    fp=os.path.join(out,name); os.makedirs(os.path.dirname(fp),exist_ok=True); open(fp,'wb').write(data)
print(src,n,'files',dict(ext),dict(sizes))
