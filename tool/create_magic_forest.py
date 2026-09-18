"""Original forest geometry for Aurea. Blender creates assets, NOT video frames.

Run Blender --background --python tool/create_magic_forest.py.
Photographic CC0 base textures: Poly Haven bark_brown_02 / forest_leaves_02.
"""
import bpy, math, random, json
from pathlib import Path
from mathutils import Vector
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/floresta-magica'
OUT.mkdir(parents=True, exist_ok=True)
random.seed(91276)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

# Original surface detail stays inside the portable GLB.
v,u=np.mgrid[0:1:512j,0:1:512j]
nrng=np.random.default_rng(912)
for name,base,leaf_tex in [('leaf_detail',(.24,.48,.14),True),('cap_amber',(.48,.20,.064),False),('cap_teal',(.07,.28,.20),False)]:
    noise=nrng.normal(0,.018,(512,512))
    if leaf_tex:
        vein=np.exp(-((u-.5)*100)**2)*.19
        side=np.exp(-(np.sin((v-abs(u-.5)*.8)*100)*12)**2)*.065
        shade=.62+.25*np.sin(u*np.pi)+vein+side+noise
    else:
        shade=.80+.16*np.cos(v*np.pi)+.045*np.sin(u*450)+noise
    rgb=np.clip(np.array(base)[None,None,:]*shade[:,:,None],0,1)
    if not leaf_tex:
        for q in range(42):
            x,y=nrng.random(2);r=nrng.uniform(.004,.016)
            spot=np.exp(-((u-x)**2+(v-y)**2)/r**2)
            rgb+=spot[:,:,None]*np.array([.22,.20,.14])[None,None,:]
    rgba=np.ones((512,512,4),dtype=np.float32);rgba[:,:,:3]=np.clip(rgb,0,1)
    image=bpy.data.images.new(name,width=512,height=512,alpha=True)
    image.pixels.foreach_set(rgba.ravel());image.filepath_raw=str(OUT/'assets'/(name+'.png'));image.file_format='PNG';image.save()

def mat(name, color, rough=.8, texture=None, emission=0, metal=0):
    m=bpy.data.materials.new(name); m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Roughness'].default_value=rough
    p.inputs['Metallic'].default_value=metal
    p.inputs['Emission Color'].default_value=(*color,1)
    p.inputs['Emission Strength'].default_value=emission
    if texture:
        t=m.node_tree.nodes.new('ShaderNodeTexImage')
        t.image=bpy.data.images.load(str(OUT/'assets'/texture)); t.image.pack()
        m.node_tree.links.new(t.outputs['Color'],p.inputs['Base Color'])
    return m

materials=[
    mat('Casca fotografica',(1,1,1),texture='bark_brown_02.jpg'),
    mat('Chao de folhas',(1,1,1),texture='forest_leaves_02.jpg'),
    mat('Folhas esmeralda',(1,1,1),texture='leaf_detail.png'),
    mat('Folhas novas',(1,1,1),texture='leaf_detail.png'),
    mat('Samambaias',(1,1,1),texture='leaf_detail.png'),
    mat('Musgo',(.14,.23,.047)),
    mat('Pedra molhada',(.11,.16,.15),.56),
    mat('Cogumelo marfim',(.61,.51,.33),.65),
    mat('Cogumelo ambar',(1,1,1),.65,texture='cap_amber.png',emission=.015),
    mat('Lamelas luminosas',(.24,1,.68),.5,emission=.38),
    mat('Cogumelo azul',(1,1,1),.58,texture='cap_teal.png'),
    mat('Orvalho dourado',(1,.62,.12),.3,emission=.3),
    mat('Agua do riacho',(.027,.14,.15),.12,metal=.6),
    mat('Folhas secas',(.27,.125,.033)),
]
groups={i:[[],[],[]] for i in range(len(materials))}

def add(mi, vs, fs, uv=None):
    a,b,c=groups[mi]; n=len(a); a.extend([tuple(v) for v in vs])
    b.extend([tuple(n+k for k in f) for f in fs]); c.extend(uv or [(0,0)]*len(vs))

def tube(points,radii,mi=0,sides=9,detail=.08):
    ps=[Vector(p) for p in points];vs=[];fs=[];uv=[]
    distance=0
    for i,p in enumerate(ps):
        if i: distance+=(p-ps[i-1]).length
        d=(ps[min(i+1,len(ps)-1)]-ps[max(0,i-1)]).normalized()
        a=d.cross(Vector((0,1,0)))
        if a.length<.01:a=d.cross(Vector((1,0,0)))
        a.normalize();b=d.cross(a).normalized()
        for j in range(sides+1):
            u=j/sides;ang=u*math.tau
            r=radii[i]*(1+detail*math.sin(j*2.7+i*.48))
            vs.append(p+r*(a*math.cos(ang)+b*math.sin(ang)))
            uv.append((u*max(1,radii[0]*4),distance*.45))
        if i:
            for j in range(sides):
                k=i*(sides+1)+j; fs.append((k-sides-1,k-sides,k+1,k))
    add(mi,vs,fs,uv)

def leaf(p,d,length,width,mi=2):
    p=Vector(p);d=Vector(d).normalized();a=d.cross(Vector((0,0,1)))
    if a.length<.01:a=d.cross(Vector((1,0,0)))
    a.normalize()
    add(mi,[p,p+d*length*.24+a*width*.72,p+d*length*.63+a*width,
            p+d*length,p+d*length*.63-a*width,p+d*length*.24-a*width*.72,
            p+d*length*.48+Vector((0,0,length*.08))],
            [(i,(i+1)%6,6) for i in range(6)],
            [(.5,0),(.14,.24),(0,.63),(.5,1),(1,.63),(.86,.24),(.5,.48)])

def ground(x,y):
    creek=math.exp(-((x-(1.5+math.sin(y*.34)*.65))/.8)**2)
    return .10*math.sin(x*.8+y*.45)+.12*math.sin(y*.65)+.08*math.sin(x*2.1)*math.cos(y*1.8)-.20*creek

vs=[];uv=[];fs=[]; n=82
for i in range(n+1):
    y=-12+i*34/n
    for j in range(n+1):
        x=-16+j*32/n;vs.append((x,y,ground(x,y)));uv.append((x*.35,y*.35))
for i in range(n):
    for j in range(n):
        a=i*(n+1)+j;fs.append((a,a+1,a+n+2,a+n+1))
add(1,vs,fs,uv)

def tree(x,y,h,r,hero=False):
    z=ground(x,y);phase=random.random()*6
    trunk=[Vector((x+.17*math.sin(i*.38+phase)*i/10,y+.11*math.cos(i*.4+phase),z+h*i/15)) for i in range(16)]
    rs=[r*(1-i/18)**1.1*(1+.5*math.exp(-i)) for i in range(16)]
    tube(trunk,rs,sides=18 if hero else 11)
    for k in range(9 if hero else 4):
        a=k*math.tau/(9 if hero else 4)+phase
        le=r*random.uniform(2.3,4.7)
        pts=[]
        for i in range(10):
            t=i/9; xx=x+math.cos(a+t*.35)*le*t; yy=y+math.sin(a+t*.35)*le*t
            pts.append((xx,yy,ground(xx,yy)+r*.62*(1-t)**2))
        tube(pts,[r*.32*(1-i/10)**1.4+.006 for i in range(10)],sides=9)
    for k in range(11 if hero else 5):
        level=random.randint(7,12);start=trunk[level];ang=k*2.399+phase
        length=h*random.uniform(.20,.40)
        end=start+Vector((math.cos(ang)*length,math.sin(ang)*length,length*.30))
        pts=[start.lerp(end,i/8)+Vector((0,0,.3*math.sin(i/8*math.pi))) for i in range(9)]
        tube(pts,[r*.39*(1-i/9)**1.3 for i in range(9)],sides=8)
        for b in range(4 if hero else 3):
            t=.35+b*.2;p=pts[min(8,int(t*8))]
            aa=ang+(-1 if b%2 else 1)*random.uniform(.4,1.1)
            e=p+Vector((math.cos(aa)*length*.65,math.sin(aa)*length*.65,.6))
            tube([p,p.lerp(e,.5),e],[r*.13,r*.07,.009],sides=5)
            for j in range(40 if hero else 20):
                q=p.lerp(e,random.random())+Vector((random.uniform(-.5,.5),random.uniform(-.5,.5),random.uniform(-.2,.5)))
                leaf(q,(random.uniform(-1,1),random.uniform(-1,1),random.uniform(-.2,.6)),random.uniform(.22,.42),random.uniform(.07,.15),random.choice([2,2,3]))

tree(0,3,7.6,.73,True)
# Foreground framing and increasingly dense background; open central sightline.
for x,y,h,r in [(-3,-3,10,.58),(3.5,-1,11,.6),(-4,3,9,.47),(4,5,10,.5),(-2.7,7,12,.48)]:tree(x,y,h,r)
for k in range(36):
    x=random.uniform(-13,13);y=random.uniform(5,21)
    if abs(x)<2 and y<9:continue
    tree(x,y,random.uniform(7,13),random.uniform(.18,.43))

def fern(x,y,s):
    z=ground(x,y)
    for k in range(6):
        a=k*math.tau/6+random.uniform(-.15,.15)
        pts=[]
        for j in range(17):
            t=j/16;pts.append(Vector((x+math.cos(a)*s*t,y+math.sin(a)*s*t,z+s*(.15+.9*math.sin(t*2)))))
        tube(pts,[.008*s*(1-i/18) for i in range(17)],4,sides=4)
        for j in range(2,16):
            t=j/16;le=s*.24*math.sin(t*math.pi)**.7
            for sign in [-1,1]:
                direction=Vector((math.cos(a+sign*1.12),math.sin(a+sign*1.12),.10))
                leaf(pts[j],direction,le,le*.17,4 if j%3 else 3)

for x,y,s in [(-1.2,-3,1.15),(2,-4,.9),(-2.4,-.3,.85),(3,1,1),(-2,4,1.05)]:fern(x,y,s)
for k in range(60):
    x=random.uniform(-8,8);y=random.uniform(-7,14)
    if abs(x)<.8 and y<3:continue
    fern(x,y,random.uniform(.3,.8))

# Thousands of individually bent grass blades and fallen leaves.
for k in range(5800):
    x=random.uniform(-10,10);y=random.uniform(-9,18)
    if abs(x-.2*math.sin(y))<.6 and y<3 and random.random()<.8:continue
    if abs(x-(1.5+math.sin(y*.34)*.65))<.55:continue
    z=ground(x,y);a=random.random()*math.tau;h=random.uniform(.06,.3);w=h*.055
    p=Vector((x,y,z));d=Vector((math.cos(a),math.sin(a),0));s=Vector((-d.y,d.x,0))*w
    add(random.choice([2,3,5]),[p-s,p+s,p+d*h*.25+Vector((0,0,h*.7)),p+d*h*.6+Vector((0,0,h))],[(0,1,2),(0,2,3)])
for k in range(600):
    x=random.uniform(-5,5);y=random.uniform(-7,10)
    leaf((x,y,ground(x,y)+.012),(random.uniform(-1,1),random.uniform(-1,1),.05),random.uniform(.08,.21),.038,13)

def ellipsoid(p,r,mi,segments=10,rings=6,jag=0):
    vs=[];fs=[];uv=[]
    for i in range(rings+1):
        a=math.pi*i/rings
        for j in range(segments+1):
            b=math.tau*j/segments; q=1+jag*math.sin(j*5+i*9)
            vs.append((p[0]+r[0]*math.sin(a)*math.cos(b)*q,p[1]+r[1]*math.sin(a)*math.sin(b)*q,p[2]+r[2]*math.cos(a)*q))
            uv.append((j/segments,i/rings))
            if i and j:
                k=i*(segments+1)+j;fs.append((k-segments-2,k-segments-1,k,k-1))
    add(mi,vs,fs,uv)

for k in range(100):
    y=random.uniform(-7,16);x=1.5+math.sin(y*.34)*.65+random.choice([-1,1])*random.uniform(.5,1.0)
    s=random.uniform(.10,.38);ellipsoid((x,y,ground(x,y)+.02),(s,s*.8,s*.55),5 if k%3 else 6,jag=.15)

def mushroom(x,y,h,r,blue=False):
    z=ground(x,y);tilt=random.uniform(-.1,.1)
    tube([(x+tilt*t,y,z+h*t) for t in [0,.25,.5,.75,1]],[r*.12,r*.10,r*.10,r*.12,r*.15],7,sides=9)
    vs=[];fs=[];uv=[]; nr=9; ns=24
    for i in range(nr+1):
        t=i/nr
        for j in range(ns+1):
            a=math.tau*j/ns;rr=r*math.sin(t*math.pi*.52)
            vs.append((x+tilt+rr*math.cos(a),y+rr*math.sin(a),z+h+r*.5*math.cos(t*math.pi*.52)+.008*math.sin(a*7)*t))
            uv.append((j/ns,t))
            if i and j:
                k=i*(ns+1)+j;fs.append((k-ns-2,k-ns-1,k,k-1))
    add(10 if blue else 8,vs,fs,uv)
    # Continuous cupped underside: radial detail stays inside the cap outline.
    under=[(x+tilt,y,z+h-.04)];uf=[];uu=[(.5,.5)]
    for j in range(49):
        a=j*math.tau/48
        under.append((x+tilt+r*.97*math.cos(a),y+r*.97*math.sin(a),z+h-.008))
        uu.append((.5+.5*math.cos(a),.5+.5*math.sin(a)))
        if j:uf.append((0,j+1,j))
    add(9 if blue else 7,under,uf,uu)
    for j in range(12):
        a=random.random()*math.tau;rr=r*random.uniform(.1,.85)
        ellipsoid((x+tilt+rr*math.cos(a),y+rr*math.sin(a),z+h+r*.5*math.sqrt(1-(rr/r)**2)+.007),(.012,.012,.007),7,6,3)

for cx,cy in [(-1.15,-1.2),(-.8,2.4),(2.3,1.2),(-2.6,4.5),(2.6,6)]:
    for k in range(10):
        x=cx+random.uniform(-.48,.48);y=cy+random.uniform(-.5,.5);h=random.uniform(.14,.55)
        mushroom(x,y,h,h*random.uniform(.4,.75),k%3!=0)

# Winding shallow stream with actual rippled geometry.
vs=[];fs=[];uv=[]
for i in range(145):
    y=-10+i*29/144;cx=1.5+math.sin(y*.34)*.65
    for j in range(7):
        x=cx+(j/6-.5)*1.13
        vs.append((x,y,-.085+.015*math.sin(y*16+x*8)));uv.append((j/6,y))
        if i and j:
            k=i*7+j;fs.append((k-8,k-7,k,k-1))
add(12,vs,fs,uv)

for k in range(70):
    a=random.random()*math.tau;r=random.uniform(.7,2.2);z=random.uniform(.25,3.3)
    ellipsoid((math.cos(a)*r,3+math.sin(a)*r,z),(.015,.015,.015),11,6,4)

objects=[]
for mi,(vs,fs,uv) in groups.items():
    if not vs:continue
    me=bpy.data.meshes.new(materials[mi].name);me.from_pydata(vs,[],fs);me.update()
    ob=bpy.data.objects.new(materials[mi].name,me);bpy.context.collection.objects.link(ob);ob.data.materials.append(materials[mi])
    layer=me.uv_layers.new()
    for poly in me.polygons:
        poly.use_smooth=True
        for li in poly.loop_indices:layer.data[li].uv=uv[me.loops[li].vertex_index]
    objects.append(ob)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.gltf(filepath=str(OUT/'FLORESTA.glb'),export_format='GLB',use_selection=True,export_animations=False,export_yup=True,export_image_format='AUTO')
lo=[min(v.co[i] for ob in objects for v in ob.data.vertices) for i in range(3)]
hi=[max(v.co[i] for ob in objects for v in ob.data.vertices) for i in range(3)]
center=[(lo[i]+hi[i])/2 for i in range(3)]
meta={'center':[center[0],center[2],-center[1]],'size':max(hi[i]-lo[i] for i in range(3))/2*100,
      'triangles':sum(len(p.vertices)-2 for o in objects for p in o.data.polygons),'materials':len(objects),
      'textures':['https://polyhaven.com/a/bark_brown_02','https://polyhaven.com/a/forest_leaves_02']}
(OUT/'geometry.json').write_text(json.dumps(meta,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'FLORESTA.blend'))
print('FOREST READY',json.dumps(meta))
