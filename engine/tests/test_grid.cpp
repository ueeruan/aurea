#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/timeline/GridLayout.hpp"
#include "aurea/render/Renderer.hpp"
#include <cmath>
#include "aurea/project/FileIO.hpp"
using namespace aurea;
AUREA_TEST(GridLayout, LayoutsMorphProximityAndIndividualAnimation) {
    Engine e;EngineConfig cfg;cfg.disableAutosave=true;cfg.workerCount=2;
    AUREA_CHECK(e.initialize(cfg).ok());AUREA_CHECK(e.new_project(1000,1000,30,nullptr).ok());
    std::vector<u64> ids;for(u32 i=0;i<6;++i){auto r=e.add_shape(0);AUREA_CHECK(r.ok());if(!r.ok())return;ids.push_back(*r);}
    auto result=e.create_grid(ids.data(),static_cast<u32>(ids.size()));AUREA_CHECK(result.ok());if(!result.ok())return;
    auto* comp=e.project()->timeline().composition(e.project()->timeline().root());
    auto* controller=comp->layer(LayerId::unpack(*result));auto& effect=controller->effects.front();
    auto* item=comp->layer(LayerId::unpack(ids[0]));
    const auto rect=grid::evaluate(*comp,*item,0);
    AUREA_CHECK_NEAR(rect.matrix.col[3].x,-220,.001f);AUREA_CHECK_NEAR(rect.matrix.col[3].y,-110,.001f);
    effect.params[0].constant=ParamValue::scalar(1);auto radial=grid::evaluate(*comp,*item,0);
    AUREA_CHECK_NEAR(radial.matrix.col[3].x,350,.001f);AUREA_CHECK_NEAR(radial.matrix.col[3].y,0,.001f);
    effect.params[0].constant=ParamValue::scalar(0);effect.params[11].constant=ParamValue::scalar(50);
    auto morph=grid::evaluate(*comp,*item,0);AUREA_CHECK_NEAR(morph.matrix.col[3].x,65,.001f);
    effect.params[11].constant=ParamValue::scalar(0);
    auto& track=controller->tracks.get_or_create(TrackProperty::EffectParam,effect.id,11*4);track.set(FrameIndex{0},0);track.set(FrameIndex{30},100);
    AUREA_CHECK_NEAR(grid::evaluate(*comp,*item,15).matrix.col[3].x,65,.001f);
    effect.params[15].constant=ParamValue::scalar(-220);effect.params[16].constant=ParamValue::scalar(-110);effect.params[19].constant=ParamValue::scalar(100);effect.params[23].constant=ParamValue::scalar(0);
    AUREA_CHECK_NEAR(grid::evaluate(*comp,*item,0).opacity,0,.001f);
    effect.params[19].constant=ParamValue::scalar(0);
    const auto original=layer_world_matrix(*comp,*item,FrameIndex{0});
    item->transform.position.x+=17;
    const auto edited=layer_world_matrix(*comp,*item,FrameIndex{0});
    AUREA_CHECK_NEAR(edited.col[3].x-original.col[3].x,17,.001f);
    for(u32 mode=0;mode<4;++mode){effect.params[0].constant=ParamValue::scalar(static_cast<f32>(mode));for(auto id:ids){const auto p=grid::evaluate(*comp,*comp->layer(LayerId::unpack(id)),0);for(auto col:p.matrix.col)AUREA_CHECK(std::isfinite(col.x)&&std::isfinite(col.y)&&std::isfinite(col.z));}}
    // Save/load retains the controller, membership, parameter keyframes and offsets.
    AUREA_CHECK(e.save_project("aurea_test_grid.aurea").ok());
    AUREA_CHECK(e.load_project("aurea_test_grid.aurea").ok());
    comp=e.project()->timeline().composition(e.project()->timeline().root());
    controller=comp->layer(LayerId::unpack(*result));item=comp->layer(LayerId::unpack(ids[0]));
    AUREA_CHECK(controller&&item);if(!controller||!item)return;
    AUREA_CHECK_EQ(item->parent,LayerId::unpack(*result));
    controller->effects.front().params[0].constant=ParamValue::scalar(0);
    const auto duplicate=comp->duplicate_layer(LayerId::unpack(ids[0]),FrameIndex{0});
    AUREA_CHECK(duplicate.valid());
    auto a=grid::evaluate(*comp,*item,0).matrix.col[3],b=grid::evaluate(*comp,*comp->layer(duplicate),0).matrix.col[3];
    AUREA_CHECK(a.x!=b.x||a.y!=b.y);
    AUREA_CHECK(comp->remove_layer(duplicate));
    AUREA_CHECK_NEAR(grid::evaluate(*comp,*item,15).matrix.col[3].x,65,.001f);
    e.shutdown();fileio::remove_file("aurea_test_grid.aurea");
}
AUREA_TEST(GridLayout, FullCompositionRefusesWithoutMutation) {
    Engine e;EngineConfig cfg;cfg.disableAutosave=true;cfg.workerCount=2;
    AUREA_CHECK(e.initialize(cfg).ok());AUREA_CHECK(e.new_project(100,100,30,nullptr).ok());
    auto* comp=e.project()->timeline().composition(e.project()->timeline().root());
    for(u32 i=0;i<kMaxLayerCount;++i)(void)comp->add_layer(LayerKind::Shape,"layer");
    const u64 id=comp->order().at(0).pack();
    AUREA_CHECK(!e.create_grid(&id,1).ok());AUREA_CHECK_EQ(comp->order().size(),kMaxLayerCount);
    AUREA_CHECK(!comp->layer(LayerId::unpack(id))->parent.valid());e.shutdown();
}
