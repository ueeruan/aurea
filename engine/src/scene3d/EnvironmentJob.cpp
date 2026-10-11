#include "aurea/scene3d/EnvironmentJob.hpp"
#include "aurea/core/Log.hpp"

#include <utility>

namespace aurea::scene3d {

bool start_environment_job(std::future<EnvironmentMaps>& job, std::shared_ptr<const HdriPixels> pixels,
                           const EnvironmentQuality& quality) noexcept {
    if (job.valid()) return false;
    try {
        job = std::async(std::launch::async, [pixels = std::move(pixels), quality] {
            if (pixels && pixels->width > 0 && !pixels->rgb.empty())
                return build_environment_from_equirect(pixels->rgb.data(), pixels->width, pixels->height, quality);
            return build_studio_environment(quality);
        });
        return true;
    } catch (...) {
        AUREA_LOG_WARN("3D: nao foi possivel iniciar o ambiente do objeto");
        return false;
    }
}

bool take_environment_job(std::future<EnvironmentMaps>& job, EnvironmentMaps& maps) noexcept {
    if (!job.valid()) return false;
    try {
        maps = job.get();
        return true;
    } catch (...) {
        AUREA_LOG_WARN("3D: ambiente do objeto nao foi gerado");
        return false;
    }
}

} // namespace aurea::scene3d
