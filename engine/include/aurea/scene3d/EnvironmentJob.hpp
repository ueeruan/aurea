#pragma once

#include "aurea/scene3d/Environment.hpp"

#include <future>

namespace aurea::scene3d {

// Only this boundary enables exceptions: launching a worker can fail even
// when the deterministic map builder itself has a noexcept contract.
// A live future is never replaced, because std::async destruction may wait.
[[nodiscard]] bool start_environment_job(std::future<EnvironmentMaps>& job,
                                         std::shared_ptr<const HdriPixels> pixels,
                                         const EnvironmentQuality& quality) noexcept;
[[nodiscard]] bool take_environment_job(std::future<EnvironmentMaps>& job,
                                        EnvironmentMaps& maps) noexcept;

} // namespace aurea::scene3d
