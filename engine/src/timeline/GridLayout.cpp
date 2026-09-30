#include "aurea/timeline/GridLayout.hpp"
#include "aurea/vector/Vector.hpp"
#include <cmath>
namespace aurea::grid {
void declare_parameters(ParameterRegistry& p) {
    static const char* modes[]={"Retangular","Radial","Caminho","Esfera"};
    static const char* orientations[]={"Frente","Centro","Tangente"};
    static const char* paths[]={"Linha","Onda","Espiral"};
    p.add_enum("mode","Layout",modes,4,0);
    p.add_int("columns","Colunas",3,1,512);
    p.add_float("spacing_x","Espaçamento X",220,0,4000);
    p.add_float("spacing_y","Espaçamento Y",220,0,4000);
    p.add_float("radius","Raio",350,0,10000);
    p.add_angle("angle","Ângulo inicial",0);
    p.add_angle("sweep","Arco",360,-720,720);
    p.add_float("depth","Profundidade",0,-4000,4000);
    p.add_enum("orientation","Orientação",orientations,3,0);
    p.add_enum("path","Caminho padrão",paths,3,0);
    p.add_enum("target","Layout de destino",modes,4,1);
    p.add_float("morph","Transição",0,0,100,kParamAnimatable|kParamPercent,"%");
    p.add_float("scale","Escala dos elementos",100,0,1000,kParamAnimatable|kParamPercent,"%");
    p.add_angle("twist","Rotação por elemento",0);
    p.add_float("delay","Atraso por elemento",0,0,120,kParamAnimatable,"frames");
    p.add_float("effector_x","Proximidade X",0,-10000,10000);
    p.add_float("effector_y","Proximidade Y",0,-10000,10000);
    p.add_float("effector_z","Proximidade Z",0,-10000,10000);
    p.add_float("range","Raio de proximidade",300,1,10000);
    p.add_float("strength","Força de proximidade",0,0,100,kParamAnimatable|kParamPercent,"%");
    p.add_float("proximity_scale","Escala próxima",150,0,1000,kParamAnimatable|kParamPercent,"%");
    p.add_angle("proximity_rotation","Rotação próxima",0);
    p.add_float("attract","Atrair / repelir",0,-100,100,kParamAnimatable|kParamPercent,"%");
    p.add_float("proximity_opacity","Opacidade próxima",100,0,100,kParamAnimatable|kParamPercent,"%");
    p.add_layer_ref("path_layer","Camada de caminho");
    p.add_float("path_offset","Deslocamento no caminho",0,-100,100,kParamAnimatable|kParamPercent,"%");
}
Placement evaluate(const Composition& comp,const Layer& child,f64 frame) {
    Placement out; const auto* controller=child.parent.valid()?comp.layer(child.parent):nullptr;
    if(!controller)return out;
    const EffectInstance *fx=nullptr,*item=nullptr;
    for(const auto& e:controller->effects)if(e.enabled&&e.type==effect_type_id(kController)){fx=&e;break;}
    for(const auto& e:child.effects)if(e.enabled&&e.type==effect_type_id(kItem)){item=&e;break;}
    if(!fx||!item||item->params.size()<2)return out;
    // Membership is live: duplication and deletion cannot leave stale counts
    // or two elements occupying the same slot. Equal order values use layer id.
    const f32 order=std::clamp(item->params[0].constant.as_float(),0.f,511.f);
    u64 childId=0; for (auto id:comp.order().raw()) if(comp.layer(id)==&child) {childId=id.pack();break;}
    f32 index=0; u32 count=0;
    for (auto id:comp.order().raw()) {
        const auto* member=comp.layer(id); if(!member || member->parent!=child.parent)continue;
        for(const auto& e:member->effects)if(e.enabled&&e.type==effect_type_id(kItem)&&!e.params.empty()) {
            ++count; const f32 key=std::clamp(e.params[0].constant.as_float(),0.f,511.f);
            if(key<order || (key==order&&id.pack()<childId))++index;
            break;
        }
    }
    count=std::max(1u,count);
    static const ParameterRegistry parameters=[] {ParameterRegistry p;declare_parameters(p);return p;}();
    const f64 local=frame-controller->start.value+controller->offset.value;
    auto sample=[&](u32 i,f64 t) {
        const auto f=static_cast<i64>(std::floor(t));
        auto a=evaluate_param(controller->tracks,*fx,i,parameters.at(i),FrameIndex{f});
        if(parameters.at(i).type==ParamType::Float||parameters.at(i).type==ParamType::Angle) {
            const auto b=evaluate_param(controller->tracks,*fx,i,parameters.at(i),FrameIndex{f+1});a.v[0]+=(b.v[0]-a.v[0])*static_cast<f32>(t-f);
        }
        return a;
    };
    const f64 t=local-index*sample(14,local).as_float();
    auto p=[&](u32 i){const f32 v=sample(i,t).as_float();return std::isfinite(v)?v:parameters.at(i).defaultValue.as_float();};
    const f32 radius=p(4), angle=p(5)*kDeg2Rad, sweep=p(6)*kDeg2Rad;
    const f32 u=count>1?index/static_cast<f32>(count-1):.5f;
    struct Point {Vec3 pos;f32 tangent=0;};
    auto layout=[&](u32 mode)->Point {
        if(mode==0) {
            const u32 columns=std::max(1u,static_cast<u32>(p(1))),rows=(count+columns-1)/columns;
            const u32 i=static_cast<u32>(index);
            return {{(static_cast<f32>(i%columns)-(columns-1)*.5f)*p(2),(static_cast<f32>(i/columns)-(rows-1)*.5f)*p(3),0},0};
        }
        if(mode==1){const f32 a=angle+sweep*index/(std::abs(std::abs(sweep)-2*kPi)<.001f?count:std::max(1u,count-1));return {{radius*std::cos(a),radius*std::sin(a),p(7)*u},a+kPi*.5f};}
        if(mode==3){const f32 y=1-2*(index+.5f)/count,ring=std::sqrt(std::max(0.f,1-y*y)),a=angle+index*kPi*(3-std::sqrt(5.f));return {{radius*ring*std::cos(a),radius*y,radius*ring*std::sin(a)},a};}
        const auto ref=sample(24,t).ref;
        const auto* guide=ref?comp.layer(LayerId::unpack(ref)):nullptr;
        if(guide&&guide!=controller&&guide!=&child&&!guide->shape.vector.groups.empty()){
            const auto& group=guide->shape.vector.groups.front();
            if(!group.paths.empty()){
                const auto g=vector::evaluate_group(group,guide->tracks,0,frame-guide->start.value+guide->offset.value);
                vector::Contour c;vector::flatten(vector::transform_path(vector::path_at(g.paths.front(),frame-guide->start.value+guide->offset.value),vector::group_matrix(g)),1.f,c);
                Vec2 point,tangent;
                if(vector::sample_at(c,(u+p(25)*.01f)*vector::length_of(c),point,tangent))return {{point.x-guide->shape.bounds.w*.5f,point.y-guide->shape.bounds.h*.5f,p(7)*u},std::atan2(tangent.y,tangent.x)};
            }
        }
        const f32 v=u+p(25)*.01f,x=(v-.5f)*p(2)*std::max(1u,count-1);
        if(p(9)<.5f)return {{x,0,p(7)*v},0};
        if(p(9)<1.5f){const f32 a=angle+v*sweep;return {{x,radius*std::sin(a),p(7)*v},std::atan2(radius*sweep*std::cos(a),p(2)*std::max(1u,count-1))};}
        const f32 a=angle+v*sweep;return {{radius*v*std::cos(a),radius*v*std::sin(a),p(7)*v},a+kPi*.5f};
    };
    const auto a=layout(static_cast<u32>(p(0))),b=layout(static_cast<u32>(p(10)));const f32 morph=p(11)*.01f;
    Vec3 pos=a.pos+(b.pos-a.pos)*morph;
    f32 rotation=p(13)*index*kDeg2Rad;
    const u32 orientation=static_cast<u32>(p(8));
    if(orientation==1)rotation+=std::atan2(-pos.y,-pos.x);
    if(orientation==2)rotation+=a.tangent+std::remainder(b.tangent-a.tangent,2*kPi)*morph;
    const Vec3 delta=Vec3{p(15),p(16),p(17)}-pos;
    f32 influence=std::clamp(1-delta.length()/std::max(1.f,p(18)),0.f,1.f);
    influence=influence*influence*(3-2*influence)*p(19)*.01f;
    pos=pos+delta*(influence*p(22)*.01f); rotation+=p(21)*kDeg2Rad*influence;
    const f32 scale=p(12)*.01f*(1+(p(20)*.01f-1)*influence);
    out.opacity=1+(p(23)*.01f-1)*influence;
    out.matrix=Mat4::translation(pos)*Mat4::from_quat(Quat::from_euler_zyx(0,0,rotation))*Mat4::scale(Vec3{scale,scale,scale});
    return out;
}
}
