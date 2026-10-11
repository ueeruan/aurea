// Native export QA entry point. It creates no preview surface or Swift UI.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "TestFramework.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern "C" {
#include <libavutil/log.h>
}

int main(int argc, char** argv) {
    @autoreleasepool {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        if (argc != 3 || chdir(argv[1]) != 0) {
            std::fprintf(stderr, "Owned export QA directory unavailable: %s\n", std::strerror(errno));
            return 2;
        }
        if (setenv("AUREA_BENCH_GLTF", argv[2], 1) != 0) return 2;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::fprintf(stderr, "Actual iOS simulator Metal device unavailable\n");
            return 3;
        }
        std::printf("Actual iOS simulator Metal device: %s\n", device.name.UTF8String);
        av_log_set_level(AV_LOG_ERROR);
        // The GPU case asserts availability and verifies every decoded output
        // picture. Audio cases also verify nested clocks and encoded AAC tones.
        (void)aurea::test::run_all("OfflineNativeGpu");
        (void)aurea::test::run_all("OfflineAudio");
        (void)aurea::test::run_all("FFmpegExport");
        (void)aurea::test::run_all("OfflineFrameQueue");
        (void)aurea::test::run_all("ExportPixelCopy");
        return static_cast<int>(aurea::test::Registry::instance().failures());
    }
}
