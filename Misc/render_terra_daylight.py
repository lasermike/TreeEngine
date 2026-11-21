import bpy, math, pathlib, numpy as np, argparse, sys, json
from mathutils import Vector, Matrix
parser=argparse.ArgumentParser(description="Render the Terra daylight environment in Blender")
parser.add_argument("output", help="Scratch/output directory")
parser.add_argument("--sun", nargs=3, type=float, default=(-.6259292648824133,.7141802822623909,-.3133034947002239), metavar=("X","Y","Z"), help="Direction toward the sun in TreeEngine coordinates")
parser.add_argument("--diameter", type=float, default=2.0, help="Visible sun diameter in degrees")
parser.add_argument("--scale", type=float, default=.045, help="World radiance scale")
args=parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
out=pathlib.Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True)
sun=Vector(args.sun)
if sun.length < 1e-8 or sun.y <= 0 or not 0 < args.diameter <= 10 or not 0 < args.scale <= 1:
    raise ValueError("Sun must be above the horizon; diameter must be in (0,10] and scale in (0,1]")
sun.normalize()
(out/"sky-metadata.json").write_text(json.dumps({"sun_direction":list(sun),"angular_diameter_degrees":args.diameter,"radiance_scale":args.scale},indent=2))
bpy.ops.wm.read_factory_settings(use_empty=True)
s=bpy.context.scene
s.render.engine='CYCLES';s.cycles.device='CPU';s.cycles.samples=1
s.render.resolution_x=256;s.render.resolution_y=256;s.render.resolution_percentage=100
s.render.image_settings.file_format='OPEN_EXR';s.render.image_settings.color_mode='RGBA';s.render.image_settings.color_depth='32'
s.render.film_transparent=False;s.view_settings.view_transform='Standard'
world=bpy.data.worlds.new('Terra daylight');s.world=world;world.use_nodes=True
nodes=world.node_tree.nodes;nodes.clear();sky=nodes.new('ShaderNodeTexSky');sky.sky_type='MULTIPLE_SCATTERING';sky.sun_disc=False
sky.sun_elevation=math.asin(sun.y)
sky.sun_rotation=math.atan2(sun.x,-sun.z)
bg=nodes.new('ShaderNodeBackground');bg.inputs['Strength'].default_value=1
wo=nodes.new('ShaderNodeOutputWorld');world.node_tree.links.new(sky.outputs['Color'],bg.inputs['Color']);world.node_tree.links.new(bg.outputs['Background'],wo.inputs['Surface'])
cam=bpy.data.cameras.new('Cubemap');cam.type='PERSP';cam.lens_unit='FOV';cam.angle=math.pi/2
obj=bpy.data.objects.new('Cubemap',cam);s.collection.objects.link(obj);s.camera=obj
faces=[((1,0,0),(0,1,0)),((-1,0,0),(0,1,0)),((0,1,0),(0,0,-1)),((0,-1,0),(0,0,1)),((0,0,1),(0,1,0)),((0,0,-1),(0,1,0))]
def to_blender(v):return Vector((v[0],-v[2],v[1]))
out.mkdir(parents=True,exist_ok=True)
arrays=[]
for index,(fd,ud) in enumerate(faces):
 f=to_blender(fd);u=to_blender(ud);right=f.cross(u);matrix=Matrix((right,u,-f)).transposed().to_4x4();obj.matrix_world=matrix
 s.render.filepath=str(out/('sky-face-%d.exr'%index))
 bpy.ops.render.render(write_still=True)
 im=bpy.data.images.load(s.render.filepath,check_existing=False)
 pixels=np.empty(256*256*4,dtype=np.float32);im.pixels.foreach_get(pixels)
 pixels=pixels.reshape(256,256,4)[::-1,:,:].copy();arrays.append(pixels);bpy.data.images.remove(im)
 print('FACE',index,'mean',np.mean(pixels[:,:,:3],axis=(0,1)),'max',np.max(pixels[:,:,:3]),flush=True)
np.save(out/'nishita-faces.npy',np.stack(arrays))
print('SKY_READY',flush=True)

