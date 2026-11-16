// AE reference batch B: one registration point, appended after every other
// builtin, so the registry order of the older effects does not change.
// Card Wipe (APC CardWipeCam), Card Dance (APC CardDanceCam) and Shatter
// (APC Shatter). Evidence: build/reference/effects-20261115/batch-b/status.json.
#include "CardReference.hpp"

namespace aurea::builtin {

void register_reference_batch_b(EffectRegistry& registry) {
    card_reference::register_effects(registry);
}

} // namespace aurea::builtin
