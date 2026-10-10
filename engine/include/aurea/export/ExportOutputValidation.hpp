#pragma once
#include "aurea/core/Types.hpp"
namespace aurea {
// Validate the finalized temporary container using a DIFFERENT decoder from
// the encoder. Frame timestamps are checked against this absolute output clock.
struct ExportOutputValidation {
    u32 width = 0, height = 0, frames = 0;
    f64 fps = 30;
    bool audio = false;
    u64 stallNs = 30'000'000'000ull;
};
} // namespace aurea
