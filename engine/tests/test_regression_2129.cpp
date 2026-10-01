#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/core/GestureMath.hpp"
#include "aurea/scene3d/Importer.hpp"
#include "aurea/scene3d/Animation.hpp"
#include "ufbx.h"
#include <fstream>
#include <limits>

using namespace aurea;
using namespace aurea::scene3d;
namespace {
struct Rig2129 {
    Engine e;
    Rig2129() { EngineConfig c; c.workerCount=1; c.disableAutosave=true;
        AUREA_CHECK(e.initialize(c).ok()); AUREA_CHECK(e.new_project(640,360,30,nullptr).ok()); }
    ~Rig2129(){e.shutdown();}
    Composition* comp(){return e.project()->timeline().composition(e.project()->timeline().current());}
};
}

AUREA_TEST(Regression2129, ShadowStrengthIsBoundedUndoableAndPersistent) {
    Rig2129 r; const auto id=r.e.add_light(0); AUREA_CHECK(id.ok()); if(!id.ok())return;
    Command c; c.type=CommandType::LayerSetLightParam;
    c.shape_param={LayerId::unpack(*id),10,.35f}; AUREA_CHECK(r.e.apply_command(c).ok());
    f32 values[12]{}; values[11]=1234;
    AUREA_CHECK(r.e.query_light(*id,values,11)); AUREA_CHECK_NEAR(values[10],.35f,1e-6f); AUREA_CHECK_EQ(values[11],1234.f);
    values[10]=999; AUREA_CHECK(r.e.query_light(*id,values)); AUREA_CHECK_EQ(values[10],999.f);
    for (f32 invalid : {-1.f,1.1f,std::numeric_limits<f32>::quiet_NaN()}) {
        c.shape_param.value=invalid; AUREA_CHECK(!r.e.apply_command(c).ok());
    }
    c.shape_param.param=11; c.shape_param.value=.2f; AUREA_CHECK(!r.e.apply_command(c).ok());
    c.type=CommandType::Undo; AUREA_CHECK(r.e.apply_command(c).ok());
    AUREA_CHECK(r.e.query_light(*id,values,11)); AUREA_CHECK_EQ(values[10],1.f);
    c.type=CommandType::Redo; AUREA_CHECK(r.e.apply_command(c).ok());
    AUREA_CHECK(r.e.save_project("build/regression-2129-shadow.aurea").ok());
    AUREA_CHECK(r.e.load_project("build/regression-2129-shadow.aurea").ok());
    r.comp()->layers().for_each([&](LayerId,const Layer& l){ if(l.kind==LayerKind::Light)AUREA_CHECK_NEAR(l.light.shadowStrength,.35f,1e-6f); });
}

AUREA_TEST(Regression2129, PreviewPanUsesCapturedParentAndCameraBasis) {
    Rig2129 r; auto* comp=r.comp();
    const auto parent=comp->add_layer(LayerKind::Null,"parent");
    const auto child=comp->add_layer(LayerKind::Null,"child");
    auto* p=comp->layer(parent); auto* l=comp->layer(child);
    p->threeD=l->threeD=true; p->start=l->start=FrameIndex{0}; p->end=l->end=FrameIndex{120};
    p->transform.position={320,180,0}; p->transform.rotation={20,35,15}; p->transform.scale={2,.7f,1.4f};
    l->parent=parent; l->transform.position={0,0,40}; l->transform.rotation={10,20,30};
    f32 basis[13]{}; AUREA_CHECK(r.e.query_preview_gesture_basis(child.pack(),basis));
    f32 before[8]{},after[8]{}; AUREA_CHECK(r.e.query_gizmo(child.pack(),1,before));
    l->transform.position=preview_gesture_value(basis,45,-23,false);
    AUREA_CHECK(r.e.query_gizmo(child.pack(),1,after));
    AUREA_CHECK_NEAR(after[0]-before[0],45,.02f); AUREA_CHECK_NEAR(after[1]-before[1],-23,.02f);
    const auto rotation=preview_gesture_value(basis,36,18,true);
    AUREA_CHECK_NEAR(rotation.x,1,.001f); AUREA_CHECK_NEAR(rotation.y,38,.001f); AUREA_CHECK_NEAR(rotation.z,30,.001f);
    // Returning the fingers to their origin restores the exact original values.
    l->transform.position=preview_gesture_value(basis,0,0,false);
    AUREA_CHECK_NEAR(l->transform.position.z,40,1e-6f);
    l->locked=true; AUREA_CHECK(!r.e.query_preview_gesture_basis(child.pack(),basis));
}

AUREA_TEST(Regression2129, GroupedFbxKeepsInstanceMaterialsAndGeometryTransforms) {
    const char* path="build/regression-2129-grouped.fbx";
    std::ofstream file(path);
    file << R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: { FBXHeaderVersion: 1003
 FBXVersion: 7400
}
GlobalSettings: { Version: 1000
 Properties70: {
 P: "UpAxis", "int", "Integer", "",1
 P: "UpAxisSign", "int", "Integer", "",1
 P: "FrontAxis", "int", "Integer", "",2
 P: "FrontAxisSign", "int", "Integer", "",1
 P: "CoordAxis", "int", "Integer", "",0
 P: "CoordAxisSign", "int", "Integer", "",1
 P: "UnitScaleFactor", "double", "Number", "",100
 }
}
Objects: {
 Geometry: 1000, "Geometry::Shared", "Mesh" {
 Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
 PolygonVertexIndex: *3 { a: 0,1,-3 }
 LayerElementMaterial: 0 {
  Version: 101
  MappingInformationType: "AllSame"
  ReferenceInformationType: "IndexToDirect"
  Materials: *1 { a: 0 }
 }
 Layer: 0 { Version: 100
  LayerElement: { Type: "LayerElementMaterial"
   TypedIndex: 0
  }
 }
 }
 Model: 2000, "Model::Group", "Null" {
 Properties70: {
 P: "Lcl Translation", "Lcl Translation", "", "A",3,4,5
 P: "Lcl Rotation", "Lcl Rotation", "", "A",20,35,15
 P: "Lcl Scaling", "Lcl Scaling", "", "A",2,0.7,1.4
 }
 }
 Model: 2100, "Model::Red", "Mesh" {
 Properties70: {
 P: "Lcl Translation", "Lcl Translation", "", "A",1,2,0
 P: "GeometricTranslation", "Vector3D", "Vector", "",0,0,2
 }
 }
 Model: 2200, "Model::Blue", "Mesh" {
 Properties70: {
 P: "Lcl Translation", "Lcl Translation", "", "A",-2,0,1
 P: "Lcl Rotation", "Lcl Rotation", "", "A",0,30,0
 P: "GeometricTranslation", "Vector3D", "Vector", "",0,3,0
 P: "GeometricRotation", "Vector3D", "Vector", "",0,0,45
 }
 }
 Material: 3000, "Material::Red", "" { Version: 102
 ShadingModel: "phong"
 Properties70: { P: "DiffuseColor", "Color", "", "A",1,0,0 }
 }
 Material: 3100, "Material::Blue", "" { Version: 102
 ShadingModel: "phong"
 Properties70: { P: "DiffuseColor", "Color", "", "A",0,0,1 }
 }
}
Connections: {
 C: "OO",2000,0
 C: "OO",2100,2000
 C: "OO",2200,2000
 C: "OO",1000,2100
 C: "OO",1000,2200
 C: "OO",3000,2100
 C: "OO",3100,2200
}
)FBX";
    file.close();
    auto imported=import_scene_file(path,ImportOptions{}); AUREA_CHECK(imported.ok()); if(!imported.ok())return;
    ufbx_load_opts opts{}; opts.target_axes=ufbx_axes_right_handed_y_up;opts.target_unit_meters=1;
    auto* reference=ufbx_load_file(path,&opts,nullptr); AUREA_CHECK(reference!=nullptr); if(!reference)return;
    const auto& asset=*imported.asset; const auto world=asset.rest_world_matrices();
    u32 instances=0; bool red=false,blue=false;
    for(usize i=0;i<asset.nodes.size();++i){const auto& node=asset.nodes[i];if(node.mesh<0)continue;
        ++instances; const auto& primitive=asset.meshes[node.mesh].primitives[0];
        const auto material=node.material_for(primitive); AUREA_CHECK(material>=0);
        std::printf("\n    instance %s: material=%d, slots=%zu, primitive slot=%d\n",node.name.c_str(),material,node.materials.size(),primitive.materialSlot);
        if(material>=0){const auto& c=asset.materials[material].baseColor;red|=c.x>.9f&&c.z<.1f;blue|=c.z>.9f&&c.x<.1f;}
        // Match world vertices against both source instances, including geometry-only transforms.
        for(const auto& vertex:primitive.positions){ const Vec3 actual=world[i].transform_point(vertex); f32 error=1e9f;
            for(usize n=0;n<reference->nodes.count;++n){const auto* rn=reference->nodes.data[n];if(!rn->mesh)continue;
                for(usize v=0;v<rn->mesh->vertices.count;++v){const auto q=ufbx_transform_position(&rn->geometry_to_world,rn->mesh->vertices.data[v]);
                    error=std::min(error,(actual-Vec3{static_cast<f32>(q.x),static_cast<f32>(q.y),static_cast<f32>(q.z)}).length());}
            }
            AUREA_CHECK(error<.0001f);
        }
    }
    AUREA_CHECK_EQ(instances,2u); AUREA_CHECK(red); AUREA_CHECK(blue); ufbx_free_scene(reference);
}
