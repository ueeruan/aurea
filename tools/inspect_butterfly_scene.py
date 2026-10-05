import bpy, json
from mathutils import Vector
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=r'C:/Users/Ruan/Downloads/butterfly_diorama.glb')
info=[]
for o in bpy.context.scene.objects:
    if o.type=='MESH':
        pts=[o.matrix_world @ Vector(p) for p in o.bound_box]
        info.append({'name':o.name,'min':[min(p[i] for p in pts) for i in range(3)],'max':[max(p[i] for p in pts) for i in range(3)],'vertices':len(o.data.vertices),'materials':[m.name for m in o.data.materials]})
print('SCENE_INFO',json.dumps(info))
print('ANIM',[(a.name,a.frame_range[:]) for a in bpy.data.actions])
prefs=bpy.context.preferences.addons['cycles'].preferences
prefs.compute_device_type='OPTIX';prefs.get_devices()
print('DEVICES',[(d.name,d.type) for d in prefs.devices])
