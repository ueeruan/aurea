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
//
//  SAÍDA QUE SE CURA SOZINHA: interrupção (ligação, Siri, outro app) desativa
//  a sessão, e nem sempre chega o aviso de fim; troca de rota / taxa reconfigura
//  e PARA o AVAudioEngine; "media services reset" mata o grafo inteiro. Antes,
//  qualquer um deles deixava o som mudo até reiniciar o app. Agora: o start
//  reativa a sessão se ela caiu e, se o engine não sobe, refaz o grafo e tenta
//  de novo; a mudança de configuração e o reset recriam/reiniciam o engine
//  se tocava. E o motor (AudioEngine) vigia o callback: parou com o play
//  pedido, ele fecha e reabre esta saída. Tudo que mexe no AVAudioEngine passa
//  por @synchronized — o mixer do núcleo e as notificações chegam em threads
//  diferentes.
// =============================================================================
#include "AureaBridge.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include "aurea/core/Log.hpp"
#include "aurea/audio/PlanarOutput.hpp"

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
@property (atomic, strong, nullable) AVAudioEngine* engine;
@property (atomic, strong, nullable) AVAudioSourceNode* source;
@property (atomic, assign) BOOL wantPlaying;
@property (atomic, assign) BOOL interrupted;
/// A sessão está ativa (setActive:YES deu certo e nada a derrubou desde então).
@property (atomic, assign) BOOL sessionActive;
/// A classe C++ fechou esta saída: nada de religar o engine (notificação atrasada).
@property (atomic, assign) BOOL closed;

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
    void* _slot;   ///< o struct POD do bloco de render (para refazer o grafo)
    /// As notificações do sistema só agendam aqui: nunca seguram o lock do
    /// host dentro de uma fila interna do AVFoundation (evita deadlock com um
    /// start/stop em andamento na thread do mixer).
    dispatch_queue_t _queue;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _outputSampleRate = (double)aurea::audio::kMixRate;
        _slot = nullptr;
        _queue = dispatch_queue_create("aurea.audio.recover", DISPATCH_QUEUE_SERIAL);
        // O AVAudioEngine para sozinho quando a configuração de E/S muda (rota,
        // taxa do hardware): se tocava, sobe de novo.
        [NSNotificationCenter.defaultCenter addObserver:self
                                               selector:@selector(onConfigurationChange:)
                                                   name:AVAudioEngineConfigurationChangeNotification
                                                 object:nil];
        // O servidor de mídia reiniciou: sessão e grafo antigos não valem mais.
        [NSNotificationCenter.defaultCenter addObserver:self
                                               selector:@selector(onMediaServicesReset:)
                                                   name:AVAudioSessionMediaServicesWereResetNotification
                                                 object:nil];
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
    const BOOL active = [session setActive:YES error:error];
    self.sessionActive = active;
    return active;
}

- (void)closeSession {
    self.sessionActive = NO;
    [AVAudioSession.sharedInstance setActive:NO
                                 withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                       error:nil];
}

/// Reativa a sessão se algo a derrubou (interrupção sem aviso de fim, outro
/// app tomou o áudio, reset do servidor de mídia). Categoria de novo também:
/// depois de um reset ela volta ao padrão.
- (BOOL)ensureSession:(NSError**)error {
    if (self.sessionActive) return YES;
    return [self openSession:error];
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
    // Standard AVAudioFormat is NON-INTERLEAVED Float32. The core produces
    // interleaved stereo; writing it directly into mBuffers[0] both corrupts
    // the channel order and overruns that mono buffer.
    aurea::ios::AudioRenderSlot* renderSlot = (aurea::ios::AudioRenderSlot*)slot;
    AVAudioSourceNode* source =
        [[AVAudioSourceNode alloc] initWithFormat:format
                                     renderBlock:^OSStatus(BOOL* isSilence,
                                                           const AudioTimeStamp* timestamp,
                                                           AVAudioFrameCount frameCount,
                                                           AudioBufferList* outputData) {
        (void)timestamp;
        if (!outputData || outputData->mNumberBuffers != 2
            || outputData->mBuffers[0].mNumberChannels != 1
            || outputData->mBuffers[1].mNumberChannels != 1) {
            if (outputData) for (UInt32 i = 0; i < outputData->mNumberBuffers; ++i) {
                AudioBuffer& b = outputData->mBuffers[i];
                if (b.mData) std::memset(b.mData, 0, b.mDataByteSize);
            }
            if (isSilence) *isSilence = YES;
            return noErr;
        }
        auto fn = reinterpret_cast<aurea::audio::AudioRenderFn>(renderSlot->fn.load(std::memory_order_acquire));
        const bool rendered = aurea::audio::render_planar_stereo(fn, renderSlot->ctx,
            static_cast<f32*>(outputData->mBuffers[0].mData), outputData->mBuffers[0].mDataByteSize,
            static_cast<f32*>(outputData->mBuffers[1].mData), outputData->mBuffers[1].mDataByteSize,
            static_cast<u32>(frameCount));
        if (isSilence) *isSilence = !rendered;
        return noErr;
    }];
    if (!source) {
        if (error) *error = [NSError errorWithDomain:@"aurea.audio" code:2 userInfo:nil];
        return NO;
    }
    [engine attachNode:source];
    [engine connect:source to:engine.mainMixerNode format:format];
    engine.mainMixerNode.outputVolume = 1.0f;
    @synchronized(self) {
        _slot = slot;
        self.engine = engine;
        self.source = source;
    }
    return YES;
}

/// Desmonta e monta o grafo de novo (engine que não sobe, reset de mídia).
- (BOOL)rebuildEngine:(NSError**)error {
    void* slot = nullptr;
    @synchronized(self) {
        if (self.closed) return NO;
        slot = _slot;
        [self tearDown];
    }
    return slot ? [self buildEngineWithSlot:slot error:error] : NO;
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
    @synchronized(self) {
        if (self.closed) return NO;
        // Sessão derrubada (interrupção sem aviso de fim, reset): sem reativar,
        // o start falha para sempre.
        if (![self ensureSession:error]) return NO;
        if (!self.engine && ![self rebuildEngine:error]) return NO;
        AVAudioEngine* engine = self.engine;
        if (engine.isRunning) return YES;
        // `prepare` antes do `start`: abrir o hardware leva alguns ms, e sem ele
        // eles caem no primeiro callback (um estalo).
        [engine prepare];
        if ([engine startAndReturnError:error]) return YES;
        // Grafo preso numa configuração velha (rota/taxa mudou com ele parado):
        // refaz o grafo e tenta uma vez mais, com a sessão reativada.
        AUREA_LOG_WARN("audio: engine nao subiu (%s); refazendo o grafo", aurea_describe(error ? *error : nil));
        self.sessionActive = NO;
        if (![self ensureSession:error] || ![self rebuildEngine:error]) return NO;
        engine = self.engine;
        [engine prepare];
        return [engine startAndReturnError:error];
    }
}

- (void)stopEngine {
    @synchronized(self) {
        AVAudioEngine* engine = self.engine;
        if (engine) [engine pause];
    }
}

- (void)tearDown {
    @synchronized(self) {
        AVAudioEngine* engine = self.engine;
        if (engine) {
            [engine stop];
            if (self.source) [engine detachNode:self.source];
        }
        self.source = nil;
        self.engine = nil;
    }
}

/// Volta a tocar se o play está pedido (notificações do sistema). Assíncrono,
/// na fila própria do host.
- (void)resumeIfWanted:(const char*)why {
    dispatch_async(_queue, ^{
        if (!self.wantPlaying || self.interrupted) return;
        AVAudioEngine* engine = self.engine;
        if (engine && engine.isRunning) return;
        NSError* error = nil;
        if (![self startEngine:&error]) {
            // O vigia do motor fecha e reabre a saída se o callback não voltar.
            AUREA_LOG_WARN("audio: nao voltou depois de %s (%s)", why, aurea_describe(error));
        }
    });
}

- (void)onInterruption:(NSNotification*)note {
    const AVAudioSessionInterruptionType type =
        (AVAudioSessionInterruptionType)[note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
    if (type == AVAudioSessionInterruptionTypeBegan) {
        // O sistema desativou a sessão: o próximo start reativa (o aviso de
        // fim pode nunca chegar).
        self.interrupted = YES;
        self.sessionActive = NO;
        return;
    }
    self.interrupted = NO;
    self.sessionActive = NO;
    [self resumeIfWanted:"a interrupcao"];
}

- (void)onRouteChange:(NSNotification*)note {
    (void)note;
    [self resumeIfWanted:"a troca de rota"];
}

- (void)onConfigurationChange:(NSNotification*)note {
    // Só o NOSSO engine (outros AVAudioEngine do app, se houver, não contam).
    if (note.object != self.engine) return;
    [self resumeIfWanted:"a mudanca de configuracao"];
}

- (void)onMediaServicesReset:(NSNotification*)note {
    (void)note;
    // Tudo o que existia morreu: sessão e grafo novos.
    self.sessionActive = NO;
    dispatch_async(_queue, ^{
        NSError* error = nil;
        if (![self rebuildEngine:&error]) {
            AUREA_LOG_WARN("audio: grafo nao voltou depois do reset de midia (%s)", aurea_describe(error));
            return;
        }
        [self resumeIfWanted:"o reset de midia"];
    });
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
            // Play do usuário: tenta mesmo que um aviso de fim de interrupção
            // nunca tenha chegado (o start reativa a sessão).
            host.interrupted = NO;
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
            if (!host_) return;
            AureaAudioHost* host = (__bridge_transfer AureaAudioHost*)host_;
            host_ = nullptr;
            host.wantPlaying = NO;
            host.closed = YES;
            [host tearDown];
            // Stop/join the render graph before changing the callback context.
            slot_.ctx = nullptr;
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
            if (!nodeTime || !nodeTime.isSampleTimeValid) return false;
            const double rate = [host outputSampleRate];
            const double mix = (double)audio::kMixRate;
            frames = rate > 0.0 ? (i64)llround((double)nodeTime.sampleTime * mix / rate)
                                : (i64)nodeTime.sampleTime;
            return frames >= 0;
        }
    }

    u32 latency_frames() const noexcept override {
        @autoreleasepool {
            AureaAudioHost* host = host_ref();
            if (!host) return 0;
            double seconds = 0.0;
            if (host.engine) seconds += host.engine.outputNode.presentationLatency;
            const double io = AVAudioSession.sharedInstance.IOBufferDuration;
            if (io > 0.0) seconds += io;
            if (seconds <= 0.0) {
                // Estimativa do buffer padrão quando o sistema não informa:
                // melhor um valor conhecido do que zero ("latência nenhuma").
                seconds = 0.005;
            }
            // AudioOutput's clock uses mixer frames, including 44.1 kHz routes.
            return (u32)llround(seconds * (double)audio::kMixRate);
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
