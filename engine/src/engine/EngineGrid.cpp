#include "aurea/Engine.hpp"
#include "aurea/timeline/GridLayout.hpp"
#include <algorithm>
namespace aurea {
Result<u64> Engine::create_grid(const u64* ids,u32 count) noexcept {
    if(!ids||!count||count>512)return Status{Errc::InvalidArgument};
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto* comp=project_?current_composition():nullptr;if(!comp)return Status{Errc::InvalidState};
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
    modelRevision_.fetch_add(1,std::memory_order_acq_rel);project_->mark_dirty();request_render();return controller.pack();
}
}
