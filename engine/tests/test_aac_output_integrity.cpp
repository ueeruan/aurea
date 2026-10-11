#include "TestFramework.hpp"
#include "aurea/export/AacOutputIntegrity.hpp"

#include <array>
#include <limits>

using namespace aurea;
namespace {
constexpr u8 lc48[]{0x11, 0x90};
constexpr u8 lc960[]{0x11, 0x94};
AacOutputIntegrity known(std::span<const u8> asc = lc48, u32 rate = 48000, u32 channels = 2) {
    AacOutputIntegrity a;
    a.set_format(asc, rate, channels, true);
    return a;
}
void units(AacOutputIntegrity& a, u32 count, u32 frame = 1024, i64 origin = 0, u32 rate = 48000) {
    for (u32 i = 0; i < count; ++i)
        a.encoded_buffer(100, 0, origin + static_cast<i64>(static_cast<u64>(i) * frame * 1'000'000 / rate));
}
}

AUREA_TEST(AacOutputIntegrity, PlainLcReadsNegotiated1024And960) {
    auto a = aac_lc_frame_info(lc48);
    AUREA_CHECK_EQ(a.sampleRate, 48000u); AUREA_CHECK_EQ(a.channels, 2u); AUREA_CHECK_EQ(a.frameSamples, 1024u);
    a = aac_lc_frame_info(lc960); AUREA_CHECK_EQ(a.frameSamples, 960u);
    constexpr u8 mono44[]{0x12, 0x08};
    a = aac_lc_frame_info(mono44);
    AUREA_CHECK_EQ(a.sampleRate, 44100u); AUREA_CHECK_EQ(a.channels, 1u);
    // AAC-LC, explicit rate=48000, stereo, GASpecific flags all zero.
    constexpr u8 explicitRate[]{0x17, 0x80, 0x5d, 0xc0, 0x10};
    a = aac_lc_frame_info(explicitRate);
    AUREA_CHECK_EQ(a.sampleRate, 48000u); AUREA_CHECK_EQ(a.channels, 2u); AUREA_CHECK_EQ(a.frameSamples, 1024u);
}

AUREA_TEST(AacOutputIntegrity, UnknownAscCannotCauseFalseRejection) {
    constexpr u8 invalid[][2]{{0x11,0x92}, {0x11,0x91}, {0x11,0x80}, {0x16,0x90},
                            {0x17,0x90}, {0x29,0x90}, {0xf9,0x90}, {0x11,0x98}};
    for (auto& asc : invalid) {
        auto a = known(asc); a.accepted_pcm(48000); a.encoded_buffer(10, 0, 0);
        AUREA_CHECK(!a.capacity_known()); AUREA_CHECK(!a.proves_insufficient());
    }
    for (usize size = 0; size < 2; ++size) {
        auto a = known({lc48, size}); a.accepted_pcm(48000); a.encoded_buffer(10, 0, 0);
        AUREA_CHECK(!a.capacity_known()); AUREA_CHECK(!a.proves_insufficient());
    }
    constexpr u8 extended[]{0x11,0x90,0x56,0xe5};
    auto a = known(extended); AUREA_CHECK(!a.capacity_known());
    a = known(lc48, 44100); AUREA_CHECK(!a.capacity_known());
    a = known(lc48, 48000, 1); AUREA_CHECK(!a.capacity_known());
    constexpr u8 zeroRate[]{0x17,0x80,0x00,0x00,0x10};
    AUREA_CHECK_EQ(aac_lc_frame_info(zeroRate).frameSamples, 0u);
}

AUREA_TEST(AacOutputIntegrity, SingleAuRequiresAuditedComponentAndExplicitBatchKeys) {
    AUREA_CHECK(aac_c2_single_au_contract("c2.android.aac.encoder", true, 0, true, 0));
    AUREA_CHECK(!aac_c2_single_au_contract("OMX.google.aac.encoder", true, 0, true, 0));
    AUREA_CHECK(!aac_c2_single_au_contract("c2.vendor.aac.encoder", true, 0, true, 0));
    AUREA_CHECK(!aac_c2_single_au_contract("", true, 0, true, 0));
    AUREA_CHECK(!aac_c2_single_au_contract("c2.android.aac.encoder", false, 0, true, 0));
    AUREA_CHECK(!aac_c2_single_au_contract("c2.android.aac.encoder", true, 0, false, 0));
    AUREA_CHECK(!aac_c2_single_au_contract("c2.android.aac.encoder", true, 1, true, 0));
    AUREA_CHECK(!aac_c2_single_au_contract("c2.android.aac.encoder", true, 0, true, 1));
    auto a = known(); a.set_format(lc48, 48000, 2, false);
    a.accepted_pcm(48000); a.encoded_buffer(1000, 0, 0);
    AUREA_CHECK(!a.capacity_known()); AUREA_CHECK(!a.proves_insufficient());
}

AUREA_TEST(AacOutputIntegrity, ConfigAndEmptyEosCannotRepresentPcm) {
    for (bool singleAu : {false, true}) {
        AacOutputIntegrity a; a.set_format(lc48, 48000, 2, singleAu);
        a.accepted_pcm(512); a.encoded_buffer(2, 2, 0); a.encoded_buffer(0, 4, 0);
        AUREA_CHECK_EQ(a.data_buffers(), 0ull); AUREA_CHECK(a.proves_insufficient());
    }
    auto a = known(); a.encoded_buffer(2, 2, 0); a.encoded_buffer(0, 4, 0);
    AUREA_CHECK(!a.proves_insufficient());
    a.accepted_pcm(512); a.encoded_buffer(512, 4, 0);
    AUREA_CHECK_EQ(a.data_buffers(), 1ull); AUREA_CHECK(!a.proves_insufficient());
}

AUREA_TEST(AacOutputIntegrity, ProvenCapacityShortageAndLeadingPaddingAreDistinct) {
    auto a = known(); a.accepted_pcm(48000); units(a, 46);
    AUREA_CHECK(a.capacity_known()); AUREA_CHECK(a.proves_insufficient());
    a = known(); a.accepted_pcm(48000); units(a, 47);
    AUREA_CHECK(!a.proves_insufficient());
    // A full AU-aligned second can still have hidden encoder delay. This
    // guard must not pretend to detect or remove it.
    a = known(); a.accepted_pcm(48128); units(a, 47);
    AUREA_CHECK(!a.proves_insufficient());
    a = known(); a.accepted_pcm(512); units(a, 3, 1024, -42666);
    AUREA_CHECK(a.capacity_known()); AUREA_CHECK(!a.proves_insufficient());
    a = known(); a.accepted_pcm(24000); a.accepted_pcm(24000); units(a, 46);
    AUREA_CHECK_EQ(a.accepted_frames(), 48000ull); AUREA_CHECK(a.proves_insufficient());
}

AUREA_TEST(AacOutputIntegrity, FrameLength960AndShortPcmUseCeilingCapacity) {
    auto a = known(lc960); a.accepted_pcm(1921); units(a, 2, 960);
    AUREA_CHECK(a.capacity_known()); AUREA_CHECK(a.proves_insufficient());
    a.encoded_buffer(100, 4, 40000); AUREA_CHECK(!a.proves_insufficient());
    a = known(lc960); a.accepted_pcm(960); units(a, 1, 960); AUREA_CHECK(!a.proves_insufficient());
    a = known(lc960); a.accepted_pcm(1); units(a, 1, 960); AUREA_CHECK(!a.proves_insufficient());
}

AUREA_TEST(AacOutputIntegrity, PartialUnknownFlagsOrBatchCadenceDisableComparison) {
    for (u32 flags : {8u, 16u}) {
        auto a = known(); a.accepted_pcm(48000); a.encoded_buffer(100, flags, 0);
        AUREA_CHECK(!a.capacity_known()); AUREA_CHECK(!a.proves_insufficient());
    }
    for (i64 next : {-1ll, 0ll, 42666ll}) {
        auto a = known(); a.accepted_pcm(48000);
        a.encoded_buffer(100, 0, 0); a.encoded_buffer(200, 0, next);
        AUREA_CHECK(!a.capacity_known()); AUREA_CHECK(!a.proves_insufficient());
    }
    auto a = known(); a.accepted_pcm(48000);
    a.encoded_buffer(0, 8, 0); units(a, 46);
    AUREA_CHECK(!a.capacity_known()); AUREA_CHECK(!a.proves_insufficient());
}

AUREA_TEST(AacOutputIntegrity, CountersAndTimestampExtremesAreSafe) {
    auto a = known(); a.accepted_pcm(std::numeric_limits<u64>::max()); units(a, 1);
    AUREA_CHECK(a.proves_insufficient());
    a.accepted_pcm(1); AUREA_CHECK(!a.capacity_known()); AUREA_CHECK(!a.proves_insufficient());
    a = known(); a.accepted_pcm(std::numeric_limits<u64>::max()); a.accepted_pcm(1);
    AUREA_CHECK(a.proves_insufficient()); // zero audio is proven, even with counter overflow
    a = known(); a.accepted_pcm(48000);
    a.encoded_buffer(100, 0, std::numeric_limits<i64>::min());
    a.encoded_buffer(100, 0, std::numeric_limits<i64>::max());
    AUREA_CHECK(!a.capacity_known()); AUREA_CHECK(!a.proves_insufficient());
}
