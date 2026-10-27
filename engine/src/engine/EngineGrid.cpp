#include "aurea/Engine.hpp"
#include "aurea/timeline/GridLayout.hpp"
#include <algorithm>
#include <cmath>
namespace aurea {
Result<u64> Engine::create_grid(const u64* ids,u32 count) noexcept {
    if(!ids||!count||count>512)return Status{Errc::InvalidArgument};
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto* comp=project_?current_composition():nullptr;if(!comp)return Status{Errc::InvalidState};
    if(comp->layers().count()>=kMaxLayerCount)return Status{Errc::OutOfRange};
    std::vector<LayerId> members;
    for(u32 i=0;i<count;++i){const auto id=LayerId::unpack(ids[i]);const auto* l=comp->layer(id);
        if(!l||l->locked||l->parent.valid()||l->kind==LayerKind::Camera||l->kind==LayerKind::Light||l->kind==LayerKind::Audio||l->effects.size()>=kMaxEffectCount)return Status{Errc::InvalidArgument};
        if(std::find(members.begin(),members.end(),id)==members.end())members.push_back(id);
    }
    history_.before_mutation(*comp,project_->timeline().current(),"criar grade");
    const auto controller=comp->add_layer(LayerKind::Null,"Grid Builder");
    auto* root=comp->layer(controller);root->start=FrameIndex{0};root->end=comp->duration();
    root->transform.position={comp->width()*.5f,comp->height()*.5f,0};root->threeD=true;
    EffectInstance fx;fx.id=root->alloc_effect_id();fx.type=effect_type_id(grid::kController);
    initialize_instance(fx,*effectRegistry_.params(fx.type));root->effects.push_back(std::move(fx));
    const auto now=playback_.current();
    for(u32 i=0;i<members.size();++i){
        auto* l=comp->layer(members[i]);
        const auto world=layer_world_matrix(*comp,*l,now);
        const Vec4 pivot=world*Vec4{l->transform.anchor.x,l->transform.anchor.y,l->transform.anchor.z,1};
        l->parentBasis=Mat4::translation(Vec3{-pivot.x,-pivot.y,-pivot.z})*(l->hasParentBasis?l->parentBasis:Mat4::identity());l->hasParentBasis=true;l->parent=controller;
        EffectInstance item;item.id=l->alloc_effect_id();item.type=effect_type_id(grid::kItem);initialize_instance(item,*effectRegistry_.params(item.type));
        item.params[0].constant=ParamValue::scalar(static_cast<f32>(i));item.params[1].constant=ParamValue::scalar(static_cast<f32>(members.size()));l->effects.push_back(std::move(item));
    }
    bump_model_revision_locked();project_->mark_dirty();request_render();return controller.pack();
}

// =============================================================================
// Omega grid (0.3.5): nulo de controle da grade
//
// A grade é a camada controladora (um nulo 3D com o efeito da grade) e os
// elementos são filhos dela. "Controlado por" põe a controladora como filha de
// um nulo qualquer, pelo MESMO LayerSetParent do vincular (compensação 3D
// exata em `parentBasis`): nada pula na tela no vínculo e, dali em diante, a
// grade gira, anda e escala como um corpo só em volta do pivô do nulo. A
// cadeia de pais do nulo vale inteira (nulo → nulo → …), sem teto; o mundo de
// cada pai é calculado uma vez por quadro (cache em world_chain_frac).
// =============================================================================
namespace {
Layer* grid_controller_layer(Composition* comp,u64 gridId) noexcept {
    Layer* g=comp?comp->layer(LayerId::unpack(gridId)):nullptr;
    return g&&grid::is_controller(*g)?g:nullptr;
}
}

bool Engine::is_grid_controller(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return project_&&grid_controller_layer(current_composition(),layerId)!=nullptr;
}

u64 Engine::grid_control_null(u64 gridId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if(!project_)return 0;
    Composition* comp=current_composition();
    const Layer* g=grid_controller_layer(comp,gridId);
    if(!g||!g->parent.valid())return 0;
    const Layer* p=comp->layer(g->parent);
    return p&&p->kind==LayerKind::Null?g->parent.pack():0;
}

i32 Engine::set_grid_control_null(u64 gridId,u64 nullId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if(!project_)return grid::kLinkLocked;
    // Edições na fila (arrastos) vêm antes: a compensação mede o estado final.
    drain_commands_locked();
    Composition* comp=current_composition();
    Layer* g=grid_controller_layer(comp,gridId);
    if(!g)return grid::kLinkNotGrid;
    if(g->locked)return grid::kLinkLocked;
    const LayerId gid=LayerId::unpack(gridId);
    // As duas UIs usam 0 para "Nenhum".
    const LayerId nid=nullId?LayerId::unpack(nullId):LayerId{};
    if(nid.valid()){
        const Layer* n=comp->layer(nid);
        if(!n||n->kind!=LayerKind::Null)return grid::kLinkNotNull;
        if(grid::would_cycle(*comp,gid,nid))return grid::kLinkCycle;
    }
    if(g->parent==nid||(!nid.valid()&&!g->parent.valid()))return grid::kLinkOk;
    history_.begin_group(nid.valid()?"vincular grade a nulo":"desvincular grade");
    history_.before_mutation(*comp,project_->timeline().current(),nid.valid()?"vincular grade a nulo":"desvincular grade");
    Command c;c.type=CommandType::LayerSetParent;c.layer_parent.layer=gid;c.layer_parent.parent=nid;
    const Status s=apply_command_internal(c,nullptr,true);
    history_.end_group();
    if(!s.ok())return s.code()==Errc::InvalidArgument&&nid.valid()&&grid::would_cycle(*comp,gid,nid)?grid::kLinkCycle:grid::kLinkFailed;
    bump_model_revision_locked();project_->mark_dirty();request_render();
    return grid::kLinkOk;
}

Result<u64> Engine::create_grid_control_null(u64 gridId,const char* name) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if(!project_)return Status{Errc::InvalidState};
    drain_commands_locked();
    Composition* comp=current_composition();
    Layer* g=grid_controller_layer(comp,gridId);
    if(!g)return Status{Errc::InvalidArgument,"nao e uma grade"};
    if(g->locked)return Status{Errc::InvalidState,"grade bloqueada"};
    if(comp->layers().count()>=kMaxLayerCount)return Status{Errc::OutOfRange};
    const LayerId gid=LayerId::unpack(gridId);
    const FrameIndex now=playback_.current();
    // Centro da grade = o centro dos elementos no mundo (o ponto de ancoragem de
    // cada um); sem elementos, a origem da controladora.
    Vec3 center{0,0,0};u32 members=0;
    for(u32 i=0;i<comp->order().size();++i){
        const Layer* m=comp->layer(comp->order().at(i));
        if(!m||m->parent!=gid)continue;
        const Mat4 w=layer_world_3d(*comp,*m,now);
        const Vec4 o=w*Vec4{m->transform.anchor.x,m->transform.anchor.y,m->transform.anchor.z,1};
        if(!std::isfinite(o.x)||!std::isfinite(o.y)||!std::isfinite(o.z))continue;
        center=Vec3{center.x+o.x,center.y+o.y,center.z+o.z};++members;
    }
    if(members){const f32 n=static_cast<f32>(members);center=Vec3{center.x/n,center.y/n,center.z/n};}
    else{const Vec4 o=layer_world_3d(*comp,*g,now)*Vec4{0,0,0,1};center=Vec3{o.x,o.y,o.z};}
    history_.begin_group("criar nulo de controle");
    history_.before_mutation(*comp,project_->timeline().current(),"criar nulo de controle");
    bump_model_revision_locked();
    const LayerId nid=comp->add_layer(LayerKind::Null,name&&*name?name:"Nulo 3D");
    Layer* n=comp->layer(nid);
    if(!n){history_.end_group();return Status{Errc::OutOfMemory,"camada nao criada"};}
    g=comp->layer(gid);   // add_layer pode realocar a tabela
    n->threeD=true;
    n->start=g->start;n->end=FrameIndex{std::max<i64>(g->start.value+1,g->end.value)};
    n->transform.anchor=Vec3{50.0f,50.0f,0.0f};   // caixa virtual de 100 px (alças no palco), como os outros nulos
    n->transform.position=center;
    Command c;c.type=CommandType::LayerSetParent;c.layer_parent.layer=gid;c.layer_parent.parent=nid;
    const Status s=apply_command_internal(c,nullptr,true);
    if(!s.ok()){(void)comp->remove_layer(nid);history_.end_group();return s;}
    history_.end_group();
    project_->mark_dirty();request_render();
    return nid.pack();
}
}
