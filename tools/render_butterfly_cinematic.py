"""Render the supplied GLB as a ten-second, four-shot Cycles film."""
import bpy, math, random, sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parent.parent / 'output/butterfly-cinematic'
ROOT.mkdir(parents=True, exist_ok=True)
(ROOT / 'frames').mkdir(exist_ok=True)
SOURCE = r'C:/Users/Ruan/Downloads/butterfly_diorama.glb'
preview = '--final' not in sys.argv
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SOURCE)
scene = bpy.context.scene
scene.render.fps = 24
scene.frame_start, scene.frame_end = 1, 240
scene.frame_set(1)
jar = next(o for o in scene.objects if o.type == 'MESH' and 'Jar' in o.name)
pts = [jar.matrix_world @ Vector(p) for p in jar.bound_box]
scale = 2.0 / (max(p.z for p in pts) - min(p.z for p in pts))
cx = sum(p.x for p in pts)/8; cy = sum(p.y for p in pts)/8
roots = [o for o in scene.objects if o.parent is None]
root = bpy.data.objects.new('Original diorama • normalized', None)
scene.collection.objects.link(root)
for o in roots: o.parent = root
root.scale = (scale,)*3
root.location = (-cx*scale, -cy*scale, 0)
for o in list(scene.objects):
    if o.type == 'MESH' and not o.data.materials:
        o.hide_render = True
    if o.type == 'MESH':
        for polygon in o.data.polygons: polygon.use_smooth = True
    if o.animation_data:
        for track in o.animation_data.nla_tracks:
            for strip in track.strips: strip.repeat = 3

# Real refractive glass, retaining the supplied geometry and texture colors.
glass = bpy.data.materials.get('Glass')
glass.use_nodes = True
bsdf = next(n for n in glass.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
bsdf.inputs['Base Color'].default_value = (0.97, 0.995, 0.975, 1)
bsdf.inputs['Roughness'].default_value = 0.055
bsdf.inputs['Metallic'].default_value = 0
bsdf.inputs['IOR'].default_value = 1.45
bsdf.inputs['Transmission Weight'].default_value = 1
bsdf.inputs['Alpha'].default_value = 1
if not any(m.type == 'SOLIDIFY' for m in jar.modifiers):
    solid = jar.modifiers.new('Fine physical glass wall', 'SOLIDIFY')
    solid.thickness = 0.10
    solid.offset = -1

def material(name, color, roughness=0.8):
    m = bpy.data.materials.new(name); m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Roughness'].default_value = roughness
    return m

ground = material('Forest floor • subtle organic texture', (0.025, 0.039, 0.009))
nodes = ground.node_tree.nodes; links = ground.node_tree.links
noise = nodes.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value = 9
ramp = nodes.new('ShaderNodeValToRGB')
ramp.color_ramp.elements[0].color = (0.007, 0.013, 0.004, 1)
ramp.color_ramp.elements[1].color = (0.06, 0.085, 0.016, 1)
links.new(noise.outputs['Fac'], ramp.inputs['Fac'])
links.new(ramp.outputs['Color'], nodes.get('Principled BSDF').inputs['Base Color'])
bump = nodes.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.22; bump.inputs['Distance'].default_value = 0.045
links.new(noise.outputs['Fac'], bump.inputs['Height']); links.new(bump.outputs['Normal'], nodes.get('Principled BSDF').inputs['Normal'])
bpy.ops.mesh.primitive_plane_add(size=160, location=(0,0,-0.14))
bpy.context.object.name='Continuous forest floor'; bpy.context.object.data.materials.append(ground)

# Distant trunks/foliage are deliberately outside the hero framing, forming bokeh.
random.seed(311)
bark = material('Distant bark', (0.034, 0.022, 0.012))
leaf = material('Distant foliage', (0.018, 0.065, 0.012))
for i in range(34):
    angle = random.uniform(0, 2*math.pi); radius=random.uniform(10,24)
    x,y=math.cos(angle)*radius, math.sin(angle)*radius
    height=random.uniform(8,15)
    bpy.ops.mesh.primitive_cone_add(vertices=8, radius1=random.uniform(.15,.32), radius2=.08, depth=height, location=(x,y,height/2))
    bpy.context.object.data.materials.append(bark)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=random.uniform(2,4), location=(x,y,height*.75))
    bpy.context.object.scale=(1,1,.65); bpy.context.object.data.materials.append(leaf)

world = bpy.data.worlds.new('Cool woodland ambience'); scene.world=world; world.use_nodes=True
world.node_tree.nodes['Background'].inputs['Color'].default_value=(.30,.43,.57,1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value=.22
def area(name, location, target, power, color, size):
    data=bpy.data.lights.new(name,'AREA');data.energy=power;data.color=color;data.shape='DISK';data.size=size
    obj=bpy.data.objects.new(name,data);scene.collection.objects.link(obj);obj.location=location
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
area('Warm sun through canopy',(-3,2,6),(0,0,1.3),650,(1,.72,.40),3)
area('Soft cool sky', (2,-4,4),(0,0,1.3),180,(.62,.78,1),4)
area('Glass rim', (2.5,2,3.7),(0,0,1.3),350,(1,.88,.62),2)
sun=bpy.data.lights.new('Late afternoon sun','SUN');sun.energy=1.4;sun.color=(1,.78,.51);sun.angle=.10
obj=bpy.data.objects.new('Late afternoon sun',sun);scene.collection.objects.link(obj);obj.rotation_euler=(.45,-.35,-.6)

def point(radius, angle, z):
    a=math.radians(angle);return Vector((math.sin(a)*radius,-math.cos(a)*radius,z))
def camera(name, start, end, lens, p0, p1, target, fstop):
    data=bpy.data.cameras.new(name);obj=bpy.data.objects.new(name,data);scene.collection.objects.link(obj)
    data.lens=lens;data.sensor_width=36;data.clip_start=.025;data.clip_end=200
    focus=bpy.data.objects.new(name+' focus',None);scene.collection.objects.link(focus);focus.location=target
    data.dof.use_dof=True;data.dof.focus_object=focus;data.dof.aperture_fstop=fstop;data.dof.aperture_blades=9
    for frame,pos in [(start,p0),(end,p1)]:
        obj.location=pos;obj.rotation_euler=(Vector(target)-pos).to_track_quat('-Z','Y').to_euler()
        obj.keyframe_insert(data_path='location',frame=frame);obj.keyframe_insert(data_path='rotation_euler',frame=frame)
    scene.timeline_markers.new(name,frame=start).camera=obj
    return obj
camera('01 • Establishing / slow approach',1,60,50,point(6.6,-25,2.85),point(6.15,-18,2.65),(0,0,1.30),5.6)
camera('02 • Butterfly macro',61,120,85,point(2.7,-12,1.50),point(2.5,0,1.52),(-.12,-.02,1.27),4.5)
camera('03 • Glass and wing / lateral move',121,180,65,point(4.7,52,2.1),point(4.5,65,2.00),(0,0,1.35),5.6)
camera('04 • Closing orbit / pull back',181,240,50,point(6.0,14,2.80),point(6.9,27,3.00),(0,0,1.3),5.6)

scene.render.engine='CYCLES'
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
for device in prefs.devices: device.use = device.type=='OPTIX'
scene.cycles.device='GPU';scene.cycles.samples=24 if preview else 48
scene.cycles.use_denoising=True;scene.cycles.adaptive_threshold=.035
scene.cycles.max_bounces=10;scene.cycles.transmission_bounces=8;scene.cycles.transparent_max_bounces=8
scene.render.resolution_x=960 if preview else 1920;scene.render.resolution_y=540 if preview else 1080
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB'
scene.render.film_transparent=False
scene.view_settings.view_transform='AgX';scene.view_settings.exposure=.35
scene.render.use_persistent_data=True
scene.render.filepath=str(ROOT/'frames/frame-')
scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Butterfly-Cinematic.blend'))
if preview:
    for frame in [30,90,150,210]:
        scene.frame_set(frame);scene.render.filepath=str(ROOT/f'preview-{frame:03d}.png')
        bpy.ops.render.render(write_still=True)
else:
    bpy.ops.render.render(animation=True)
