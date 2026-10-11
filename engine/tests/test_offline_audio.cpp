#include "TestFramework.hpp"
#include "NativeExportVerifier.hpp"
#include "SyntheticVideo.hpp"
#include "aurea/export/OfflineAudioBlockSource.hpp"
#include "aurea/export/OfflineAudioSnapshot.hpp"
#include "aurea/export/FFmpegExportSink.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/timeline/Composition.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <vector>

using namespace aurea;
using namespace aurea::test;
namespace {
constexpr double kTau = 6.2831853071795864769;
constexpr u64 kBusBlockBytes = u64(audio::kBlockFrames) * 2 * sizeof(f32);

class SignalBlocks final : public audio::BlockSource {
public:
    enum Mode { Ramp, Sine, Constant } mode = Ramp;
    bool missing = false;
    std::atomic<bool>* cancelOnFetch = nullptr;
    float constant = .08f;
    std::map<std::pair<u64,i64>, audio::AudioBlock> cache;
    float sample(i64 at, u32 channel) const {
        const float left = mode == Ramp ? .05f + float(at) * .00000001f :
                           mode == Sine ? float(.15 * std::sin(kTau * 440.0 * at / audio::kMixRate)) : constant;
        return channel == 0 ? left : -left * .5f;
    }
    const audio::AudioBlock* block(u64 asset, i64 index) override {
        if (cancelOnFetch) { cancelOnFetch->store(true); return nullptr; }
        if (missing) return nullptr;
        auto& result = cache[{asset,index}];
        if (result.pcm.empty()) {
            result.pcm.resize(audio::kBlockFrames * 2);
            for (u32 n=0;n<audio::kBlockFrames;++n) for (u32 c=0;c<2;++c)
                result.pcm[n*2+c] = sample(index * audio::kBlockFrames+n,c);
        }
        return &result;
    }
};
struct AudioScene {
    Project project;
    CompositionId rootId, childId;
    LayerId busId, sourceId;
    AssetId asset;
    AudioScene() {
        rootId = project.timeline().create_composition("root",192,96,30);
        childId = project.timeline().create_composition("24fps-child",192,96,24);
        project.timeline().set_root(rootId);
        root().set_duration(FrameIndex{90}); child().set_duration(FrameIndex{288});
        Asset media; media.kind=AssetKind::Audio; media.sourcePath="test-signal";
        media.audio.sampleRate=48000; media.audio.channels=2; media.audio.sampleCount=FrameIndex{576000};
        media.duration=FrameIndex{288}; media.timebaseFps=24;
        asset=project.add_asset(std::move(media));
        sourceId=child().add_layer(LayerKind::Audio,"source");
        source().source=asset; source().start=FrameIndex{0}; source().end=FrameIndex{288};
        busId=root().add_layer(LayerKind::Composition,"nested-clock");
        bus().nested.composition=childId; bus().start=FrameIndex{0}; bus().end=FrameIndex{90};
    }
    Composition& root() { return *project.timeline().composition(rootId); }
    Composition& child() { return *project.timeline().composition(childId); }
    Layer& bus() { return *root().layer(busId); }
    Layer& source() { return *child().layer(sourceId); }
    std::unique_ptr<OfflineAudioSnapshot> prepare(u64 budget=OfflineAudioSnapshot::kDefaultBlockBudget) {
        auto made=OfflineAudioSnapshot::create(root(),project,nullptr,nullptr,nullptr,budget);
        AUREA_CHECK(made.ok());
        return made.ok()?std::move(*made):nullptr;
    }
};
void check_samples(OfflineAudioSnapshot& graph, SignalBlocks& signal, i64 start, u32 n, i64 sourceAt, i64 step=1) {
    std::vector<f32> pcm(n*2);
    const auto result=graph.mix(start,n,signal,pcm.data()); AUREA_CHECK(result.ok());
    if (!result.ok()) return;
    for(u32 i=0;i<n;++i) for(u32 c=0;c<2;++c)
        AUREA_CHECK_NEAR(pcm[i*2+c],signal.sample(sourceAt+i*step,c),1e-6);
}
}

AUREA_TEST(OfflineAudio, NestedClockUsesParentSpeedAndSecondsAcrossDifferentFps) {
    AudioScene scene; SignalBlocks signal;
    scene.bus().start=FrameIndex{15}; scene.bus().offset=FrameIndex{6}; scene.bus().speed=2;
    scene.source().offset=FrameIndex{12};
    auto graph=scene.prepare(); if(!graph)return;
    check_samples(*graph,signal,36000,513,57600,2);
    // Existing default snapshot remains the playback path and its semantics.
    auto playback=audio::build_snapshot(scene.root(),scene.project,nullptr,nullptr,nullptr);
    std::vector<f32> unchanged(513*2); audio::mix(*playback,36000,513,signal,unchanged.data());
    for(u32 i=0;i<513;++i) AUREA_CHECK_NEAR(unchanged[i*2],signal.sample(48000+i,0),1e-6);
    AUREA_CHECK_EQ(scene.bus().kind,LayerKind::Composition);
    AUREA_CHECK_EQ(scene.bus().speed,2.f); AUREA_CHECK_EQ(scene.child().fps(),24.);
}

AUREA_TEST(OfflineAudio, NestedReverseAndNonMonotonicRemapReadExactSourceSamples) {
    AudioScene scene; SignalBlocks signal;
    scene.bus().speed=2; scene.bus().reversed=true;
    auto reverse=scene.prepare(); if(!reverse)return;
    check_samples(*reverse,signal,24000,512,236800,-2);
    scene.bus().reversed=false; scene.bus().timeRemapEnabled=true;
    scene.bus().timeRemap.keys={{FrameIndex{0},30.f},{FrameIndex{30},90.f},{FrameIndex{60},30.f},{FrameIndex{90},60.f}};
    auto remap=scene.prepare(); if(!remap)return;
    check_samples(*remap,signal,24000,513,96000,2);
    check_samples(*remap,signal,72000,511,96000,-2);
    // A second remapped composition level exercises graph recursion and clocks.
    const auto grandId=scene.project.timeline().create_composition("60fps-grandchild",192,96,60);
    auto* grand=scene.project.timeline().composition(grandId); grand->set_duration(FrameIndex{720});
    const auto nestedId=scene.child().add_layer(LayerKind::Composition,"third-clock");
    auto* nested=scene.child().layer(nestedId); nested->nested.composition=grandId;
    nested->end=FrameIndex{288}; nested->speed=.5f; scene.source().muted=true;
    const auto mediaId=grand->add_layer(LayerKind::Audio,"grand-source");
    auto* media=grand->layer(mediaId); media->source=scene.asset; media->end=FrameIndex{720};
    auto recursive=scene.prepare(); if(!recursive)return;
    check_samples(*recursive,signal,24000,513,48000,1);
    AUREA_CHECK_EQ(recursive->stats().compositionInstances,3u);
}

AUREA_TEST(OfflineAudio, NestedSoloGainsExpressionAndEffectsRetainOriginalTrackOwnership) {
    AudioScene scene; SignalBlocks signal; signal.mode=SignalBlocks::Constant;
    scene.bus().solo=true; scene.bus().gain=.5f; scene.bus().pan=.2f;
    auto& volume=scene.bus().tracks.get_or_create(TrackProperty::AudioVolume);
    volume.expression=expr::compile("50"); volume.expressionEnabled=true;
    scene.source().gain=.5f; scene.source().solo=true;
    const auto ignored=scene.child().add_layer(LayerKind::Audio,"non-solo-child");
    scene.child().layer(ignored)->source=scene.asset; scene.child().layer(ignored)->end=FrameIndex{288};
    const auto ignoredRoot=scene.root().add_layer(LayerKind::Audio,"non-solo-root");
    scene.root().layer(ignoredRoot)->source=scene.asset; scene.root().layer(ignoredRoot)->end=FrameIndex{90};
    EffectInstance effect; effect.id=scene.source().alloc_effect_id();
    effect.type=effect_type_id(audio::fx_keys::kStereoMixer);
    const auto* params=expr::builtin_effects().params(effect.type); AUREA_CHECK(params); if(!params)return;
    initialize_instance(effect,*params);
    effect.params[audio::fxp::kLeftLevel].constant=ParamValue::scalar(50);
    effect.params[audio::fxp::kRightLevel].constant=ParamValue::scalar(25);
    scene.source().effects.push_back(std::move(effect));
    auto graph=scene.prepare(); if(!graph)return;
    std::vector<f32> pcm(1024*2); audio::MixState state;
    AUREA_CHECK(graph->mix(24000,1024,signal,pcm.data(),nullptr,&state).ok());
    for(u32 i=0;i<1024;++i) { AUREA_CHECK_NEAR(pcm[i*2],.004f,1e-6); AUREA_CHECK_NEAR(pcm[i*2+1],-.00125f,1e-6); }
    AUREA_CHECK(volume.expression->error().ok);
}

AUREA_TEST(OfflineAudio, ChildBusesKeepHeadroomUntilTheFinalRootLimiter) {
    AudioScene scene; SignalBlocks signal; signal.mode=SignalBlocks::Constant; signal.constant=.9f;
    scene.source().gain=2; scene.bus().gain=.1f;
    auto graph=scene.prepare(); if(!graph)return;
    std::vector<f32> pcm(1024*2); AUREA_CHECK(graph->mix(0,1024,signal,pcm.data()).ok());
    for(u32 i=0;i<1024;++i) { AUREA_CHECK_NEAR(pcm[i*2],.18f,1e-6); AUREA_CHECK_NEAR(pcm[i*2+1],-.09f,1e-6); }
}

AUREA_TEST(OfflineAudio, BusCacheIsBoundedAndMissingOrCancelledSamplesCannotPublish) {
    AudioScene scene; SignalBlocks signal;
    scene.root().set_duration(FrameIndex{3600}); scene.bus().end=FrameIndex{3600};
    scene.child().set_duration(FrameIndex{2880}); scene.source().end=FrameIndex{2880};
    scene.project.asset(scene.asset)->audio.sampleCount=FrameIndex{5760000};
    auto graph=scene.prepare(2*kBusBlockBytes); if(!graph)return;
    std::vector<f32> pcm(513*2);
    for(i64 block=0;block<240;++block) {
        AUREA_CHECK(graph->mix(block*audio::kBlockFrames+7,513,signal,pcm.data()).ok());
        AUREA_CHECK(graph->stats().blockBytes<=2*kBusBlockBytes);
    }
    AUREA_CHECK(graph->stats().peakBlockBytes<=2*kBusBlockBytes);
    AUREA_CHECK_EQ(graph->stats().renderedBlocks,240ull);
    signal.missing=true;
    AUREA_CHECK(graph->mix(0,513,signal,pcm.data()).code()==Errc::DecodeFailed);
    for(float v:pcm)AUREA_CHECK_EQ(v,0.f);
    std::atomic<bool> cancel{true};
    AUREA_CHECK(graph->mix(0,513,signal,pcm.data(),nullptr,nullptr,&cancel).code()==Errc::Cancelled);
    signal.missing=false;
    cancel.store(false); signal.cancelOnFetch=&cancel;
    auto midDecode=scene.prepare(); if(!midDecode)return;
    AUREA_CHECK(midDecode->mix(0,513,signal,pcm.data(),nullptr,nullptr,&cancel).code()==Errc::Cancelled);
    signal.cancelOnFetch=nullptr; cancel.store(false);
    auto tiny=scene.prepare(kBusBlockBytes-1); if(!tiny)return;
    AUREA_CHECK(tiny->mix(0,513,signal,pcm.data()).code()==Errc::BudgetExceeded);
    for(float v:pcm)AUREA_CHECK_EQ(v,0.f);
    scene.bus().nested.composition=scene.rootId;
    const auto cycle=OfflineAudioSnapshot::create(scene.root(),scene.project,nullptr,nullptr,nullptr);
    AUREA_CHECK(cycle.code()==Errc::ProjectCorrupted);
}

AUREA_TEST(OfflineAudio, ParentBusKeepPitchMatchesExistingMixerAtTwoTimesSpeed) {
    AudioScene scene; SignalBlocks signal; signal.mode=SignalBlocks::Sine;
    scene.bus().speed=2;
    auto varispeed=scene.prepare(); if(!varispeed)return;
    scene.bus().keepPitch=true;
    auto stretched=scene.prepare(); if(!stretched)return;
    auto crossings=[&](OfflineAudioSnapshot& graph) {
        std::vector<float> pcm(24000*2); audio::MixState state;
        for(u32 at=0;at<24000;) {
            const u32 n=std::min(4096u,24000-at);
            AUREA_CHECK(graph.mix(12000+at,n,signal,pcm.data()+at*2,nullptr,&state).ok()); at+=n;
        }
        u32 count=0;
        for(u32 i=1;i<24000;++i) if((pcm[(i-1)*2]<0)!=(pcm[i*2]<0))++count;
        return count;
    };
    const u32 fastCrossings=crossings(*varispeed), pitchCrossings=crossings(*stretched);
    std::printf("\n    nested pitch: varispeed=%u, preserve=%u crossings /0.5s\n",fastCrossings,pitchCrossings);
    AUREA_CHECK(std::abs(int(fastCrossings)-880)<=2);
    AUREA_CHECK(pitchCrossings < fastCrossings * .6);
    // Existing granular pitch engine is deliberately preserved. Its grain
    // seams add zero crossings; verify the entire waveform against ordinary
    // keepPitch media playback, rather than mistaking seams for a new tone.
    audio::AudioMixSnapshot direct;
    audio::AudioClip clip; clip.asset=scene.asset.pack(); clip.end=144000; clip.sourceLength=576000;
    clip.rate=2; clip.keepPitch=true; clip.fadeTo=144000; clip.fps=30;
    direct.clips.push_back(clip); direct.endSample=144000;
    std::vector<float> busPcm(4096*2),reference(4096*2);
    for(i64 at: {12000ll,32000ll,64000ll}) {
        AUREA_CHECK(stretched->mix(at,4096,signal,busPcm.data()).ok());
        audio::mix(direct,at,4096,signal,reference.data());
        for(usize i=0;i<reference.size();++i)AUREA_CHECK_NEAR(busPcm[i],reference[i],1e-7);
    }
}


AUREA_TEST(OfflineAudio, ReusedNestedCompositionEffectsRemainIndependentAcrossChunkSizes) {
    AudioScene scene; SignalBlocks signal; signal.mode=SignalBlocks::Sine;
    auto addDelay=[](Layer& layer) {
        EffectInstance effect; effect.id=layer.alloc_effect_id(); effect.type=effect_type_id(audio::fx_keys::kDelay);
        const auto* registry=expr::builtin_effects().params(effect.type); AUREA_CHECK(registry); if(!registry)return;
        initialize_instance(effect,*registry);
        effect.params[audio::fxp::kDelayTime].constant=ParamValue::scalar(75);
        effect.params[audio::fxp::kDelayFeedback].constant=ParamValue::scalar(30);
        layer.effects.push_back(std::move(effect));
    };
    addDelay(scene.source()); addDelay(scene.bus());
    scene.bus().pan=-1; scene.bus().speed=2; scene.bus().reversed=true;
    const auto secondId=scene.root().add_layer(LayerKind::Composition,"same-child-other-clock");
    auto* second=scene.root().layer(secondId); second->nested.composition=scene.childId; second->end=FrameIndex{90};
    second->speed=.5f; second->pan=1; addDelay(*second);
    auto large=scene.prepare(),small=scene.prepare();if(!large||!small)return;
    AUREA_CHECK_EQ(large->stats().compositionInstances,3u);
    auto render=[&](OfflineAudioSnapshot& graph,u32 chunk) {
        std::vector<float> pcm(48000*2);audio::MixState state;
        for(u32 at=0;at<48000;) {const u32 count=std::min(chunk,48000-at);
            AUREA_CHECK(graph.mix(at,count,signal,pcm.data()+at*2,nullptr,&state).ok());at+=count;}
        return pcm;
    };
    const auto big=render(*large,4096),little=render(*small,257);
    double error=0,stereoDifference=0;
    for(usize i=0;i<big.size();++i)error=std::max(error,double(std::fabs(big[i]-little[i])));
    for(usize i=0;i<big.size();i+=2)stereoDifference+=std::fabs(big[i]-big[i+1]);
    AUREA_CHECK(error<1e-5);AUREA_CHECK(stereoDifference>100);
    AUREA_CHECK(large->stats().peakBlockBytes<=OfflineAudioSnapshot::kDefaultBlockBudget);
}

AUREA_TEST(OfflineAudio, RealBlockLeasesSurviveCacheEvictionUntilTheChunkEnds) {
    SyntheticConfig configuration; configuration.audioRate=48000; configuration.audioSeconds=60;
    SyntheticFactory factory(configuration);
    audio::AudioBlockCache cache(&factory,kBusBlockBytes,false);
    cache.register_asset(99,{"owned-synthetic-pcm",2880000});
    std::atomic<bool> cancelled{false}; std::atomic<u64> heartbeat{0};
    OfflineAudioBlockSource blocks(cache,cancelled,&heartbeat);
    blocks.begin_chunk();
    const auto* first=blocks.block(99,0); AUREA_CHECK(first); if(!first)return;
    std::weak_ptr<const audio::AudioBlock> lease=cache.find(99,0);
    const float sample=first->pcm[123];
    for(i64 index=1;index<=32;++index) AUREA_CHECK(blocks.block(99,index));
    AUREA_CHECK(!cache.find(99,0));
    AUREA_CHECK(!lease.expired());
    if(!lease.expired()) AUREA_CHECK_EQ(first->pcm[123],sample);
    const auto held=blocks.leased_bytes();
    AUREA_CHECK_EQ(blocks.block(99,0),first);
    AUREA_CHECK_EQ(blocks.leased_bytes(),held);
    AUREA_CHECK(held<=OfflineAudioBlockSource::kDefaultBudget);
    AUREA_CHECK(heartbeat.load()>0);
    blocks.begin_chunk();
    AUREA_CHECK_EQ(blocks.leased_bytes(),0ull);
    AUREA_CHECK(lease.expired());
}

AUREA_TEST(OfflineAudio, RealBlockLeaseBudgetCancellationAndMissingSourceAreExplicit) {
    SyntheticConfig configuration; configuration.audioRate=48000; configuration.audioSeconds=60;
    SyntheticFactory factory(configuration);
    audio::AudioBlockCache cache(&factory,kBusBlockBytes,false);
    cache.register_asset(99,{"owned-synthetic-pcm",2880000});
    std::atomic<bool> cancelled{false};
    OfflineAudioBlockSource blocks(cache,cancelled,nullptr,kBusBlockBytes*2);
    blocks.begin_chunk();
    AUREA_CHECK(blocks.block(99,0)); AUREA_CHECK(blocks.block(99,1));
    AUREA_CHECK(blocks.block(99,2)==nullptr);
    AUREA_CHECK(blocks.status().code()==Errc::BudgetExceeded);
    AUREA_CHECK_EQ(blocks.leased_bytes(),kBusBlockBytes*2);
    AUREA_CHECK_EQ(blocks.peak_leased_bytes(),kBusBlockBytes*2);
    blocks.begin_chunk();
    AUREA_CHECK(blocks.block(99,2));
    cancelled.store(true);
    AUREA_CHECK(blocks.block(99,3)==nullptr);
    AUREA_CHECK(blocks.status().code()==Errc::Cancelled);
    cancelled.store(false); blocks.begin_chunk();
    AUREA_CHECK(blocks.block(100,0)==nullptr);
    AUREA_CHECK(blocks.status().code()==Errc::DecodeFailed);
}

#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
AUREA_TEST(OfflineAudio, NativeExportKeepsTwoTimesNestedVideoAndAudioClock) {
    AudioScene scene; SignalBlocks signal; signal.mode=SignalBlocks::Sine; scene.bus().speed=2;
    auto graph=scene.prepare(); if(!graph)return;
    const auto folder=std::filesystem::path("build/reference/astra-offline-audio"); std::filesystem::create_directories(folder);
    const auto file=(folder/"nested-2x-stereo.mp4").string();
    auto sink=make_ffmpeg_export_sink(nullptr); AUREA_CHECK(sink); if(!sink)return;
    VideoStreamConfig video; video.width=192;video.height=96;video.fps=30;video.bitrateBps=1200000;
    video.preferSoftware=true;video.validateBeforePublish=true;AudioStreamConfig audioConfig;
    AUREA_CHECK(sink->open(file.c_str(),video,&audioConfig).ok());
    std::vector<u8> y(192*96),uv(192*48,128);std::vector<float> pcm(1600*2);std::vector<i16> packed(1600*2);
    audio::MixState state;
    for(u32 frame=0;frame<30;++frame) {
        for(u32 row=0;row<96;++row)for(u32 col=0;col<192;++col)y[row*192+col]=(col+frame*3)%192<96?40:210;
        AUREA_CHECK(sink->write_video(y.data(),192,uv.data(),192,std::llround(frame*1000000./30)).ok());
        AUREA_CHECK(graph->mix(frame*1600,1600,signal,pcm.data(),nullptr,&state).ok());
        audio::to_pcm16(pcm.data(),packed.size(),packed.data());
        AUREA_CHECK(sink->write_audio(packed.data(),1600,std::llround(frame*1000000./30)).ok());
    }
    AUREA_CHECK(sink->finish().ok()); AUREA_CHECK(sink->validate_output({192,96,30,30,true}).ok());
    const auto verified=verify_native_export(file,30,192,96,30,true);AUREA_CHECK(verified.ok);
    AUREA_CHECK_EQ(verified.samples,48000ull);AUREA_CHECK_EQ(verified.distinctFrameHashes,30u);
    // Fresh CPU AAC decoder measures the actual encoded nested tone, after
    // priming. A container metadata assertion cannot prove this clock.
    AVFormatContext* format=nullptr;AVCodecContext* codec=nullptr;AVPacket* packet=av_packet_alloc();AVFrame* decoded=av_frame_alloc();
    std::vector<float> decodedLeft;
    int result=avformat_open_input(&format,file.c_str(),nullptr,nullptr); AUREA_CHECK(result>=0);
    if(result>=0) {
        AUREA_CHECK(avformat_find_stream_info(format,nullptr)>=0);
        const int track=av_find_best_stream(format,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);AUREA_CHECK(track>=0);
        if(track>=0) {
            codec=avcodec_alloc_context3(avcodec_find_decoder_by_name("aac"));
            AUREA_CHECK(codec); if(codec) {
                AUREA_CHECK(avcodec_parameters_to_context(codec,format->streams[track]->codecpar)>=0);
                AUREA_CHECK(avcodec_open2(codec,codec->codec,nullptr)>=0);
                auto receive=[&] { while(avcodec_receive_frame(codec,decoded)>=0) {
                    AUREA_CHECK(decoded->format==AV_SAMPLE_FMT_FLTP);
                    const float* left=reinterpret_cast<const float*>(decoded->extended_data[0]);
                    decodedLeft.insert(decodedLeft.end(),left,left+decoded->nb_samples);av_frame_unref(decoded);
                }};
                while(av_read_frame(format,packet)>=0) {if(packet->stream_index==track){AUREA_CHECK(avcodec_send_packet(codec,packet)>=0);receive();}av_packet_unref(packet);}
                AUREA_CHECK(avcodec_send_packet(codec,nullptr)>=0);receive();
            }
        }
    }
    AUREA_CHECK(decodedLeft.size()>=36000);
    if(decodedLeft.size()>=36000) {u32 count=0;for(u32 i=12001;i<36000;++i)if((decodedLeft[i-1]<0)!=(decodedLeft[i]<0))++count;
        AUREA_CHECK(std::abs(int(count)-880)<=8);
        std::printf("\n    independently decoded nested 2x AAC: %u crossings / 0.5 s (expected 880), 30 video pictures\n",count);
    }
    av_frame_free(&decoded);av_packet_free(&packet);avcodec_free_context(&codec);avformat_close_input(&format);
}
#endif
