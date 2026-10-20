import pathlib,struct,sys
MAGIC=b"\xabKTX 20\xbb\r\n\x1a\n"
def read(path):
 d=path.read_bytes()
 if d[:12]!=MAGIC: raise ValueError("KTX2 signature")
 fmt,typ,w,h,depth,layers,faces,count,compression=struct.unpack_from("<9I",d,12)
 if (fmt,typ,depth,layers,faces,compression)!=(97,2,0,0,6,0) or w!=h: raise ValueError("Expected uncompressed RGBA16F cube")
 out=[]
 for level in range(count):
  offset,length,uncompressed=struct.unpack_from("<3Q",d,80+24*level)
  size=max(1,w>>level); facebytes=size*size*8
  if length!=facebytes*6 or uncompressed!=length: raise ValueError("Unexpected level size")
  out.append([d[offset+face*facebytes:offset+(face+1)*facebytes] for face in range(6)])
 return w,out
def downsample(face,source,target):
 step=source//target; out=bytearray(target*target*8)
 for y in range(target):
  for x in range(target):
   values=[0.,0.,0.,0.]
   for sy in range(y*step,(y+1)*step):
    for sx in range(x*step,(x+1)*step):
     pixel=struct.unpack_from("<4e",face,(sy*source+sx)*8)
     for c in range(4): values[c]+=pixel[c]
   struct.pack_into("<4e",out,(y*target+x)*8,*(v/(step*step) for v in values))
 return out
def write(path,size,levels):
 header=bytearray(148);header[:4]=b"DDS "
 struct.pack_into("<7I",header,4,124,0x0002100f,size,size,size*8,0,len(levels))
 struct.pack_into("<3I",header,76,32,4,0x30315844)
 struct.pack_into("<2I",header,108,0x00401008,0x0000fe00)
 struct.pack_into("<5I",header,128,10,3,4,1,0)
 with path.open("wb") as f:
  f.write(header)
  for face in range(6):
   for mip in levels:f.write(mip[face])
root=pathlib.Path(sys.argv[1])
size,levels=read(root/"specular.ktx2")
source_levels=(0,1,3,4,5,6,8,9,10)
small=[]
for output_level,source_level in enumerate(source_levels):
 target=(size>>2)>>output_level
 source=size>>source_level
 small.append([downsample(face,source,target) if source>target else face
               for face in levels[source_level]])
write(root/"neutral-specular.dds",size>>2,small)
size,levels=read(root/"diffuse.ktx2")
write(root/"neutral-diffuse.dds",64,[[downsample(face,size,64) for face in levels[0]]])
for name in ("neutral-diffuse.dds","neutral-specular.dds"):
 print(name,(root/name).stat().st_size)
