#pragma once
#include "aurea/Engine.hpp"
#include <string>
namespace aurea::windows {
std::wstring wide(const std::string& text);
std::string utf8(const std::wstring& text);
bool initialize_media();
void shutdown_media();
bool load_image(const char* path, ImagePixels& out, void* context);
bool save_png(const std::wstring& path, const u8* rgba, u32 w, u32 h);
/// Saída de áudio do PC: WASAPI compartilhado por evento (waveOut de reserva).
std::unique_ptr<audio::AudioOutput> make_audio_output();
/// Diagnóstico da saída de make_audio_output (teste de fumaça, painel DEV).
struct AudioOutputReport {
 const char* backend = "";
 u32 bufferFrames = 0;          ///< buffer do dispositivo
 u32 streamLatencyFrames = 0;   ///< latência do stream informada pelo WASAPI
 u64 glitches = 0;              ///< fila do dispositivo vazia tocando
 u64 reopens = 0;               ///< reaberturas (fone tirado, padrão trocado)
};
bool audio_output_report(audio::AudioOutput* out, AudioOutputReport& report);
/// Onde vai o tempo do decode de vídeo (aurea_pc_decode_bench): somas desde a abertura.
struct VideoDecodeTimings {
 u64 samples = 0;        ///< amostras lidas do decoder (entregues ou não)
 u64 frames = 0;         ///< quadros entregues com planos
 f64 readMs = 0;         ///< ReadSample (decoder do MF)
 f64 mapMs = 0;          ///< textura DXVA → memória (cópia da GPU + Map)
 f64 copyMs = 0;         ///< cópia dos planos para o quadro
 const char* path = "";  ///< caminho dos quadros (diagnóstico)
};
bool video_decode_timings(const VideoDecoderBackend* video, VideoDecodeTimings& out);
class MediaFactory final : public VideoSourceFactory {
public:
 bool probe(const char* path, MediaProbe& out) override;
 std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override;
 std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override;
};
}
