#pragma once

#include <cstdint>
#include <initializer_list>

namespace aurea {

// Probe the requested bounded-rate mode on the actual selected encoder. The
// alternate mode is tried only after every configuration for the first mode
// failed. CQ is never an option, and a failed probe leaves accepted unchanged.
template <class TryMode>
bool configure_encoder_rate_mode(uint32_t requested, TryMode&& tryMode, uint32_t& accepted) noexcept {
    const uint32_t first = requested == 0 ? 0 : 1; // app codes: 0 CBR, 1 VBR
    for (uint32_t mode : {first, 1u - first}) {
        if (tryMode(mode)) { accepted = mode; return true; }
    }
    return false;
}

} // namespace aurea
