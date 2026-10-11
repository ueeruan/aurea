// Link this executable with --wrap=AMediaCodec_dequeueOutputBuffer. The test
// suppresses one real encoded access unit while retaining the driver's EOS,
// proving that production finish cannot report a shortened MP4 as complete.
#include "MediaCodecExport.hpp"
#include <media/NdkMediaCodec.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace aurea;

static bool suppressed = false;
enum class Fault { Packet, FsyncFull, CloseIo };
static Fault fault = Fault::Packet;
static int syncedFd = -1;
static bool storageFaultInjected = false;
extern "C" ssize_t __real_AMediaCodec_dequeueOutputBuffer(AMediaCodec*, AMediaCodecBufferInfo*, int64_t);
extern "C" ssize_t __wrap_AMediaCodec_dequeueOutputBuffer(AMediaCodec* codec, AMediaCodecBufferInfo* info, int64_t timeout) {
    const ssize_t index = __real_AMediaCodec_dequeueOutputBuffer(codec, info, timeout);
    if (fault == Fault::Packet && !suppressed && index >= 0 && info->size > 0 &&
        !(info->flags & (AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG | AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM))) {
        info->size = 0;
        suppressed = true;
    }
    return index;
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd) {
    syncedFd = fd;
    if (fault == Fault::FsyncFull && !storageFaultInjected) {
        storageFaultInjected = true; errno = ENOSPC; return -1;
    }
    return __real_fsync(fd);
}
extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd) {
    const int result = __real_close(fd);
    if (fault == Fault::CloseIo && fd == syncedFd && !storageFaultInjected && result == 0) {
        storageFaultInjected = true; errno = EIO; return -1;
    }
    return result;
}
static bool check(Status status, const char* stage) {
    if (status.ok()) return true;
    std::fprintf(stderr, "%s failed: %.*s\n", stage, int(status.detail().size()), status.detail().data());
    return false;
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    if (argc == 3) {
        if (std::strcmp(argv[2], "fsync-full") == 0) fault = Fault::FsyncFull;
        else if (std::strcmp(argv[2], "close-io") == 0) fault = Fault::CloseIo;
        else return 2;
    }
    auto sink = android::make_mediacodec_export_sink(nullptr);
    VideoStreamConfig video; video.width = 320; video.height = 240; video.fps = 30;
    video.bitrateBps = 2'000'000;
    if (!check(sink->open(argv[1], video, nullptr), "open")) return 3;
    std::vector<u8> y(320 * 240, 100), uv(y.size() / 2, 128);
    for (int frame = 0; frame < 30; ++frame)
        if (!check(sink->write_video(y.data(), 320, uv.data(), 320, frame * 1'000'000ll / 30), "video")) return 4;
    const Status result = sink->finish();
    const bool expected = fault == Fault::Packet
        ? suppressed && result.code() == Errc::EncodeFailed && result.detail() == "encoder terminou sem entregar todos os quadros"
        : storageFaultInjected && result.code() == (fault == Fault::FsyncFull ? Errc::StorageFull : Errc::IoError);
    if (!expected || ::access(argv[1], F_OK) == 0) {
        std::fprintf(stderr, "FAIL suppressed=%d code=%u file_exists=%d detail=%.*s\n", int(suppressed),
            unsigned(result.code()), int(::access(argv[1], F_OK) == 0), int(result.detail().size()), result.detail().data());
        return 5;
    }
    std::printf("PASS real encoder fault=%u code=%u incomplete MP4 removed, detail=%.*s\n", unsigned(fault),
        unsigned(result.code()), int(result.detail().size()), result.detail().data());
    return 0;
}
