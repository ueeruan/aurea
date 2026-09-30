#include "BuiltinEffects.hpp"
#include "aurea/timeline/GridLayout.hpp"
namespace aurea::builtin {
namespace {
class GridControl final : public Effect {
public:
    explicit GridControl(bool item=false):item_(item){}
    const EffectInfo& info() const noexcept override {static const EffectInfo c{grid::kController,"Grid Builder","Transform",EffectClass::Domain},i{grid::kItem,"Grid Item","Transform",EffectClass::Domain};return item_?i:c;}
    void declare_parameters(ParameterRegistry& p) const override {
        if(!item_){grid::declare_parameters(p);return;}
        p.add_float("index","Ordem na grade",0,0,511,kParamNone);
        p.add_float("count","Quantidade",1,1,512,kParamHidden);
    }
    bool is_identity(const EffectEval&) const noexcept override {return true;}
private:bool item_;
};
}
void register_grid_layout_effects(EffectRegistry& r){(void)r.add(std::make_unique<GridControl>());(void)r.add(std::make_unique<GridControl>(true));}
}
