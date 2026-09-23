// =============================================================================
//  Aurea / platform / ios / bridge / IOSAudio.mm
//
//  A saída de som do iOS: AVAudioEngine + AVAudioSourceNode.
//
//  O QUE ESTE ARQUIVO NÃO FAZ, E É O PONTO: não mixa, não reamostra, não
//  conhece a timeline. O Audio Engine COMPARTILHADO do núcleo
//  (engine/src/audio/: AudioMixer + AudioEngine) já produz os quadros estéreo a
//  48 kHz — a MESMA função que o export usa. Aqui só se entrega o que ele
//  produziu. É a mesma divisão do AAudioOutput.cpp no Android, e é o que faz o
//  que se ouve no iPhone ser amostra por amostra igual ao que sai no arquivo.
//
//  THREAD DE TEMPO REAL: o bloco de render não manda mensagem ObjC, não pega
//  lock e não aloca. Ele lê dois ponteiros de um struct POD que a classe C++
//  mantém vivo — por isso o bloco captura um `void*` cru e não `self`.
//
//  RELÓGIO: o vídeo segue a amostra que está saindo. `presented` responde isso
//  com `lastRenderTime` → `playerTimeForNodeTime` (a amostra que o nó de saída
//  está consumindo), convertida para a régua do MIXER (48 kHz) — um aparelho
//  bluetooth pode estar a 44,1 kHz, e devolver quadros de outra régua faria o
//  relógio derivar.
// =============================================================================
#include "AureaBridge.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include "aurea/core/Log.hpp"

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

using namespace aurea;

namespace aurea::ios {
namespace {

/// O que o bloco de render lê. Pod, sem ObjC, sem lock: `fn` é publicado uma
/// vez no `open` e zerado no `close`, e o bloco nunca segura nada além disto.
struct AudioRenderSlot {
    std::atomic<void*> fn{nullptr};
    void* ctx = nullptr;
};

} // namespace
} // namespace aurea::ios

/// Texto legível de um NSError, para o log do motor. Nível de arquivo: é usado
/// pelo host ObjC e pela classe C++.
static const char* aurea_describe(NSError* error) {
    if (!error) return "sem detalhe";
    const char* what = error.localizedDescription.UTF8String;
    return what ? what : "sem detalhe";
}

// =============================================================================
// O host ObjC: sessão, grafo e callbacks de sistema.
// =============================================================================
@interface AureaAudioHost : NSObject
@property (nonatomic, strong, nullable) AVAudioEngine* engine;
@property (nonatomic, strong, nullable) AVAudioSourceNode* source;
@property (nonatomic, assign) BOOL wantPlaying;
@property (nonatomic, assign) BOOL interrupted;

// Declarados aqui porque a classe C++ (abaixo) e quem os chama — e o
// `@implementation` sozinho nao basta para quem emite a mensagem.
- (BOOL)openSession:(NSError**)error;
- (void)closeSession;
- (BOOL)buildEngineWithSlot:(void*)slot error:(NSError**)error;
- (double)outputSampleRate;
- (BOOL)startEngine:(NSError**)error;
- (void)stopEngine;
- (void)tearDown;
@end

@implementation AureaAudioHost {
    double _outputSampleRate;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _outputSampleRate = (double)aurea::audio::kMixRate;
        // Interrupção (ligação telefônica, Siri): o sistema já parou o som; se
        // tocava, volta quando ele devolver o áudio.
        [NSNotificationCenter.defaultCenter addObserver:self
                                               selector:@selector(onInterruption:)
                                                   name:AVAudioSessionInterruptionNotification
                                                 object:nil];
        // Troca de rota (fone, bluetooth): o AVAudioEngine reconfigura sozinho
        // na maioria dos casos, mas há aparelhos em que ele PARA. Se tocava e
        // parou, volta.
        [NSNotificationCenter.defaultCenter addObserver:self
                                               selector:@selector(onRouteChange:)
                                                   name:AVAudioSessionRouteChangeNotification
                                                 object:nil];
    }
    return self;
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (BOOL)openSession:(NSError**)error {
    AVAudioSession* session = AVAudioSession.sharedInstance;
    // Playback: o som do editor sai mesmo com o aparelho no mudo e continua com
    // a tela travada. Sem o setActive a saída nem abre.
    if (![session setCategory:AVAudioSessionCategoryPlayback error:error]) return NO;
    // 48 kHz é a taxa do mixer: pedir a preferida evita a reamostragem do
    // sistema, que custaria CPU e latência sem necessidade.
    [session setPreferredSampleRate:(double)aurea::audio::kMixRate error:nil];
    [session setPreferredIOBufferDuration:0.005 error:nil];
    return [session setActive:YES error:error];
}

- (void)closeSession {
    [AVAudioSession.sharedInstance setActive:NO
                                 withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                       error:nil];
}

/// Monta o grafo: um nó de origem (o mixer do núcleo) ligado ao mixer principal.
/// `slot` é o struct POD que o bloco de render lê — não é retido por ninguém
/// além da classe C++, que vive enquanto o motor viver.
- (BOOL)buildEngineWithSlot:(void*)slot error:(NSError**)error {
    AVAudioEngine* engine = [[AVAudioEngine alloc] init];
    AVAudioFormat* format =
        [[AVAudioFormat alloc] initStandardFormatWithSampleRate:(double)aurea::audio::kMixRate
                                                      channels:aurea::audio::kMixChannels];
    if (!format || !slot) {
        if (error) *error = [NSError errorWithDomain:@"aurea.audio" code:1 userInfo:nil];
        return NO;
    }
    // `initStandardFormatWithSampleRate:` do núcleo é FLOAT INTERCALADO, que é
    // exatamente o que `AudioRenderFn` entrega (f32* estéreo intercalado): sem
    // desintercalar por quadro dentro da thread de tempo real.
    const u32 mixChannels = aurea::audio::kMixChannels;
    aurea::ios::AudioRenderSlot* renderSlot = (aurea::ios::AudioRenderSlot*)slot;
    AVAudioSourceNode* source =
        [[AVAudioSourceNode alloc] initWithFormat:format
                                     renderBlock:^OSStatus(BOOL* isSilence,
                                                           const AudioTimeStamp* timestamp,
                                                           AVAudioFrameCount frameCount,
                                                           AudioBufferList* outputData) {
        (void)timestamp;
        f32* out = (outputData->mNumberBuffers > 0 && outputData->mBuffers[0].mData != nullptr)
                       ? (f32*)outputData->mBuffers[0].mData
                       : nullptr;
        const u32 frames = (u32)frameCount;
        if (out == nullptr) {
            *isSilence = YES;
            return noErr;
        }
        void* fn = renderSlot->fn.load(std::memory_order_acquire);
        if (fn == nullptr) {
            // Sem motor ligado: silêncio EXPLÍCITO (o isSilence deixa o sistema
            // economizar), nunca lixo do buffer.
            *isSilence = YES;
            std::memset(out, 0, (usize)frames * mixChannels * sizeof(f32));
            return noErr;
        }
        // Nada de lock, alocação ou log aqui dentro: o AudioEngine do núcleo
        // alimenta isto por um anel SPSC (ver audio/Audio.hpp).
        reinterpret_cast<aurea::audio::AudioRenderFn>(fn)(renderSlot->ctx, out, frames);
        *isSilence = NO;
        return noErr;
    }];
    if (!source) {
        if (error) *error = [NSError errorWithDomain:@"aurea.audio" code:2 userInfo:nil];
        return NO;
    }
    [engine attachNode:source];
    [engine connect:source to:engine.mainMixerNode format:format];
    engine.mainMixerNode.outputVolume = 1.0f;
    self.engine = engine;
    self.source = source;
    return YES;
}

/// A taxa do nó de saída (48 kHz em quase todo aparelho; 44,1 kHz em parte do
/// bluetooth).
- (double)outputSampleRate {
    AVAudioEngine* engine = self.engine;
    if (!engine) return _outputSampleRate;
    const double rate = [[engine outputNode] outputFormatForBus:0].sampleRate;
    if (rate > 0.0) _outputSampleRate = rate;
    return _outputSampleRate;
}

- (BOOL)startEngine:(NSError**)error {
    AVAudioEngine* engine = self.engine;
    if (!engine) return NO;
    if (engine.isRunning) return YES;
    // `prepare` antes do `start`: abrir o hardware leva alguns ms, e sem ele
    // eles caem no primeiro callback (um estalo).
    [engine prepare];
    return [engine startAndReturnError:error];
}

- (void)stopEngine {
    AVAudioEngine* engine = self.engine;
    if (engine) [engine pause];
}

- (void)tearDown {
    AVAudioEngine* engine = self.engine;
    if (engine) {
        [engine stop];
        if (self.source) [engine detachNode:self.source];
    }
    self.source = nil;
    self.engine = nil;
}

- (void)onInterruption:(NSNotification*)note {
    const AVAudioSessionInterruptionType type =
        (AVAudioSessionInterruptionType)[note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
    if (type == AVAudioSessionInterruptionTypeBegan) {
        self.interrupted = YES;
        return;
    }
    self.interrupted = NO;
    if (!self.wantPlaying) return;
    NSError* error = nil;
    if (![self startEngine:&error]) {
        AUREA_LOG_WARN("audio: nao voltou depois da interrupcao (%s)", aurea_describe(error));
    }
}

- (void)onRouteChange:(NSNotification*)note {
    (void)note;
    if (!self.wantPlaying || self.interrupted) return;
    AVAudioEngine* engine = self.engine;
    if (!engine || engine.isRunning) return;
    NSError* error = nil;
    if (![self startEngine:&error]) {
        AUREA_LOG_WARN("audio: nao voltou depois da troca de rota (%s)", aurea_describe(error));
    }
}

@end

// =============================================================================
// A implementação C++ da fronteira `audio::AudioOutput`.
//
// O host ObjC é segurado com `__bridge_retained` (posse explícita, +1 no
// `open`, −1 no `close`): a classe é C++ e a posse não pode depender de como o
// ARC trata membro de objeto em classe C++.
// =============================================================================
namespace aurea::ios {
namespace {

class AVFoundationOutput final : public audio::AudioOutput {
public:
    AVFoundationOutput() = default;
    ~AVFoundationOutput() override { close(); }

    Status open(audio::AudioRenderFn fn, void* ctx) noexcept override {
        @autoreleasepool {
            if (host_) {
                slot_.ctx = ctx;
                slot_.fn.store(reinterpret_cast<void*>(fn), std::memory_order_release);
                return OkStatus;
            }
            slot_.ctx = ctx;
            slot_.fn.store(reinterpret_cast<void*>(fn), std::memory_order_release);
            AureaAudioHost* host = [[AureaAudioHost alloc] init];
            NSError* error = nil;
            if (![host openSession:&error]) {
                AUREA_LOG_ERROR("audio: sessao recusada (%s)", aurea_describe(error));
                slot_.fn.store(nullptr, std::memory_order_release);
                return Status{Errc::NotSupported, "sessao de audio indisponivel"};
            }
            if (![host buildEngineWithSlot:&slot_ error:&error]) {
                AUREA_LOG_ERROR("audio: grafo recusado (%s)", aurea_describe(error));
                [host closeSession];
                slot_.fn.store(nullptr, std::memory_order_release);
                return Status{Errc::NotSupported, "saida de audio indisponivel"};
            }
            host_ = (__bridge_retained void*)host;
            return OkStatus;
        }
    }

    Status start() noexcept override {
        @autoreleasepool {
            AureaAudioHost* host = host_ref();
            if (!host) return Status{Errc::InvalidState, "saida de audio fechada"};
            host.wantPlaying = YES;
            NSError* error = nil;
            if (![host startEngine:&error]) {
                AUREA_LOG_ERROR("audio: start recusado (%s)", aurea_describe(error));
                host.wantPlaying = NO;
                return Status{Errc::NotSupported, "nao foi possivel abrir a saida"};
            }
            return OkStatus;
        }
    }

    void stop() noexcept override {
        @autoreleasepool {
            AureaAudioHost* host = host_ref();
            if (!host) return;
            host.wantPlaying = NO;
            [host stopEngine];
        }
    }

    void close() noexcept override {
        @autoreleasepool {
            // O bloco de render para de receber o mixer ANTES de o host sumir.
            slot_.fn.store(nullptr, std::memory_order_release);
            slot_.ctx = nullptr;
            if (!host_) return;
            AureaAudioHost* host = (__bridge_transfer AureaAudioHost*)host_;
            host_ = nullptr;
            [host tearDown];
            [host closeSession];
        }
    }

    /// Quantos quadros (na régua do MIXER, 48 kHz) já saíram no alto-falante.
    bool presented(u64 nowNs, i64& frames) noexcept override {
        (void)nowNs;
        @autoreleasepool {
            AureaAudioHost* host = host_ref();
            if (!host || !host.engine) return false;
            AVAudioEngine* engine = host.engine;
            AVAudioTime* nodeTime = engine.outputNode.lastRenderTime;
            if (!nodeTime) return false;
            AVAudioTime* player = [engine.outputNode playerTimeForNodeTime:nodeTime];
            if (!player || !player.isSampleTimeValid) return false;
            const double rate = [host outputSampleRate];
            const double mix = (double)audio::kMixRate;
            frames = rate > 0.0 ? (i64)llround((double)player.sampleTime * mix / rate)
                                : (i64)player.sampleTime;
            return frames >= 0;
        }
    }

    u32 latency_frames() const noexcept override {
        @autoreleasepool {
            AureaAudioHost* host = host_ref();
            if (!host) return 0;
            const double rate = [host outputSampleRate];
            double seconds = 0.0;
            if (host.engine) seconds += host.engine.outputNode.presentationLatency;
            const double io = AVAudioSession.sharedInstance.ioBufferDuration;
            if (io > 0.0) seconds += io;
            if (seconds <= 0.0) {
                // Estimativa do buffer padrão quando o sistema não informa:
                // melhor um valor conhecido do que zero ("latência nenhuma").
                seconds = 0.005;
            }
            return (u32)llround(seconds * (rate > 0.0 ? rate : (double)audio::kMixRate));
        }
    }

private:
    AureaAudioHost* host_ref() const noexcept { return (__bridge AureaAudioHost*)host_; }

    void* host_ = nullptr;   ///< AureaAudioHost* com posse (+1) — ver `close`
    AudioRenderSlot slot_{};
};

} // namespace

std::unique_ptr<audio::AudioOutput> make_audio_output() {
    // Nulo é permitido pelo motor (preview mudo, relógio do sistema). Aqui a
    // saída existe: se a sessão de áudio não abrir, o `open` devolve o erro e o
    // motor segue sem som — nunca fingindo que há.
    return std::unique_ptr<audio::AudioOutput>(new (std::nothrow) AVFoundationOutput());
}

} // namespace aurea::ios
