import numpy as np,pathlib,struct,math,json,argparse
parser=argparse.ArgumentParser(description="Bake sky and lighting DDS cubemaps from rendered Terra daylight faces")
parser.add_argument("output", help="Scratch/output directory used by render_terra_daylight.py")
out=pathlib.Path(parser.parse_args().output).resolve()
metadata=json.loads((out/"sky-metadata.json").read_text())
faces=np.load(out/'nishita-faces.npy')[:,:,::-1,:3].astype(np.float32)
size=faces.shape[1]; scale=float(metadata["radiance_scale"])
def dirs(face,n):
 a=(np.arange(n,dtype=np.float32)+.5)*2/n-1
 u,v=np.meshgrid(a,a)
 one=np.ones_like(u)
 vec=[(one,-v,-u),(-one,-v,u),(u,one,v),(u,-one,-v),(u,-v,one),(-u,-v,-one)][face]
 d=np.stack(vec,axis=-1);return d/np.linalg.norm(d,axis=-1,keepdims=True)
def sample(d):
 shape=d.shape[:-1];d=d.reshape(-1,3);x,y,z=d[:,0],d[:,1],d[:,2]
 ax,ay,az=np.abs(x),np.abs(y),np.abs(z)
 face=np.zeros(len(d),dtype=np.int8);sc=np.zeros(len(d),dtype=np.float32);tc=sc.copy()
 for mask,idx,a,b,den in [
 ((ax>=ay)&(ax>=az)&(x>=0),0,-z,-y,ax),((ax>=ay)&(ax>=az)&(x<0),1,z,-y,ax),
 ((ay>ax)&(ay>=az)&(y>=0),2,x,z,ay),((ay>ax)&(ay>=az)&(y<0),3,x,-z,ay),
 ((az>ax)&(az>ay)&(z>=0),4,x,-y,az),((az>ax)&(az>ay)&(z<0),5,-x,-y,az)]:
  face[mask]=idx;sc[mask]=a[mask]/den[mask];tc[mask]=b[mask]/den[mask]
 px=np.clip((sc+1)*.5*size-.5,0,size-1);py=np.clip((tc+1)*.5*size-.5,0,size-1)
 x0=px.astype(np.int32);y0=py.astype(np.int32);x1=np.minimum(x0+1,size-1);y1=np.minimum(y0+1,size-1)
 fx=(px-x0)[:,None];fy=(py-y0)[:,None]
 rgb=((1-fy)*((1-fx)*faces[face,y0,x0]+fx*faces[face,y0,x1])+fy*((1-fx)*faces[face,y1,x0]+fx*faces[face,y1,x1]))*scale
 ground=np.array([.055,.046,.036],dtype=np.float32)
 t=np.clip((-y-.02)/.2,0,1)[:,None];t=t*t*(3-2*t)
 return (rgb*(1-t)+ground*t).reshape(*shape,3).astype(np.float32)
def basis(n):
 ref=np.zeros_like(n);ref[...,1]=1
 alt=np.abs(n[...,1])>.99;ref[alt]=np.array([1,0,0],np.float32)
 tangent=np.cross(ref,n);tangent/=np.maximum(np.linalg.norm(tangent,axis=-1,keepdims=True),1e-8)
 return tangent,np.cross(n,tangent)
def pack_cube(path,arrays,fmt):
 n=arrays[0][0].shape[0];m=len(arrays[0]);bpp=4 if fmt==28 else 8
 flags=0x100f|(0x20000 if m>1 else 0);caps=0x1008|(0x400000 if m>1 else 0)
 hdr=[124,flags,n,n,n*bpp,0,m]+[0]*11+[32,4,0x30315844,0,0,0,0,0]+[caps,0xfe00,0,0,0]
 with path.open('wb') as f:
  f.write(b'DDS '+struct.pack('<31I',*hdr)+struct.pack('<5I',fmt,3,4,1,0))
  for face in arrays:
   for img in face:f.write(img.tobytes(order='C'))
def rgba(rgb,fmt):
 if fmt==28:
  c=np.clip(rgb,0,1);c=np.where(c<=.0031308,12.92*c,1.055*np.power(c,1/2.4)-.055)
  return np.rint(np.concatenate([c,np.ones((*c.shape[:-1],1))],axis=-1)*255).clip(0,255).astype(np.uint8)
 return np.concatenate([rgb,np.ones((*rgb.shape[:-1],1))],axis=-1).astype('<f2')
# Background lookup inverts horizontal axes because the sky cube is inside-out.
# The sun disk is only in the visible background, avoiding double-counting it in IBL.
sky=[]
sun=np.array(metadata["sun_direction"],dtype=np.float32)
sun/=np.linalg.norm(sun)
for f in range(6):
 d=dirs(f,512);d[...,0]*=-1;d[...,2]*=-1
 direction=d.copy();direction[...,1]=np.maximum(direction[...,1],0.02);direction/=np.linalg.norm(direction,axis=-1,keepdims=True)
 color=rgba(sample(direction),28).astype(np.float32)
 angle=np.arccos(np.clip(np.sum(d*sun,axis=-1),-1,1))
 edge=.0025;coverage=np.clip((math.radians(metadata["angular_diameter_degrees"]*0.5)+edge-angle)/(2*edge),0,1)
 coverage=coverage*coverage*(3-2*coverage)
 color[:,:,:3]=color[:,:,:3]*(1-coverage[...,None])+255*coverage[...,None]
 sky.append([np.rint(color).clip(0,255).astype(np.uint8)])
pack_cube(out/'terra-daylight-sky.dds',sky,28)
# Cosine-weighted mean radiance for Lambertian diffuse IBL.
diffuse=[]
for f in range(6):
 n=dirs(f,16);t,b=basis(n);acc=np.zeros_like(n)
 for i in range(128):
  u=(i+.5)/128;phi=i*2.3999632297
  d=t*(math.sqrt(u)*math.cos(phi))+b*(math.sqrt(u)*math.sin(phi))+n*math.sqrt(1-u)
  acc+=sample(d)
 diffuse.append([rgba(acc/128,10)])
pack_cube(out/'terra-daylight-diffuse.dds',diffuse,10)
# GGX importance-sampled environment for the existing roughness mip selection.
spec=[[] for _ in range(6)]
for f in range(6):
 for mip in range(7):
  n=dirs(f,max(1,64>>mip));rough=mip/6
  if mip==0:rgb=sample(n)
  else:
   t,b=basis(n);acc=np.zeros_like(n);weights=np.zeros(n.shape[:-1],np.float32);alpha=max(rough*rough,1e-4)
   for i in range(64):
    u=(i+.5)/64;phi=i*2.3999632297;c=1/math.sqrt(1+alpha*alpha*u/(1-u));ss=math.sqrt(max(0,1-c*c))
    h=n*c+t*(ss*math.cos(phi))+b*(ss*math.sin(phi))
    vh=np.sum(n*h,axis=-1);l=2*vh[...,None]*h-n
    nl=np.maximum(np.sum(n*l,axis=-1),0);acc+=sample(l)*nl[...,None];weights+=nl
   rgb=acc/np.maximum(weights[...,None],1e-6)
  spec[f].append(rgba(rgb,10))
pack_cube(out/'terra-daylight-specular.dds',spec,10)
print('Generated terra-daylight-sky.dds, terra-daylight-diffuse.dds, terra-daylight-specular.dds')
