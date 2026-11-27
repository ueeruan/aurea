#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/tracking/TrackerEffects.hpp"

namespace aurea::tracking {
namespace {
// The layer stores each analysis independently. These entries expose the
// controls in the effect stack without creating a render pass or textures.
class TrackerEffect final : public Effect {
public:
    explicit TrackerEffect(EffectInfo info) noexcept : info_(info) {}
    const EffectInfo& info() const noexcept override { return info_; }
    void declare_parameters(ParameterRegistry&) const override {}
    bool is_identity(const EffectEval&) const noexcept override { return true; }
    Status build(EffectBuildContext&, const EffectEval&, const LayerImage& input, f32, LayerImage& out) const override {
        out = input;
        return OkStatus;
    }

private:
    EffectInfo info_;
};
} // namespace

void register_tracker_effects(EffectRegistry& registry) {
    (void)registry.add(std::make_unique<TrackerEffect>(EffectInfo{kCameraTrackerEffect, "Camera Tracker", "Rastreio"}));
    (void)registry.add(std::make_unique<TrackerEffect>(EffectInfo{kMotionTrackerEffect, "Motion Tracker", "Rastreio"}));
}
} // namespace aurea::tracking
