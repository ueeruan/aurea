#pragma once
#include "aurea/core/Result.hpp"
#include "aurea/text/Captions.hpp"
#include <atomic>
#include <functional>
namespace aurea { class VideoSourceFactory; }
namespace aurea::text {
// The decoder and model belong to this job, never to the preview render thread.
Result<std::vector<CaptionWord>> transcribe_local(
    VideoSourceFactory& factory, const std::string& source, const std::string& model,
    const std::string& language, std::atomic<bool>& cancelled,
    const std::function<void(int)>& progress, double start = 0, double end = 0, bool translateEnglish = false) noexcept;

/// 0.4.1 (legendas na nuvem): um trecho de fala gravado em WAV 16 kHz mono 16 bits.
struct SpeechChunk { std::string path; double start = 0, end = 0; };
/// O áudio da mídia em trechos para mandar a um transcritor remoto: corta no ponto
/// mais quieto entre 25 e 45 s (a palavra não fica partida entre dois trechos) e
/// pula os trechos sem fala (silêncio não é enviado nem cobrado). Os arquivos vão
/// para `dir` (chunk_000.wav…); quem chama apaga depois.
Result<std::vector<SpeechChunk>> export_speech_chunks(
    VideoSourceFactory& factory, const std::string& source, const std::string& dir,
    std::atomic<bool>& cancelled, const std::function<void(int)>& progress) noexcept;
}
