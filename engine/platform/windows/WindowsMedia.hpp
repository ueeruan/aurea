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
std::unique_ptr<ExportSink> make_export_sink(void*);
std::unique_ptr<audio::AudioOutput> make_audio_output();
class MediaFactory final : public VideoSourceFactory {
public:
 bool probe(const char* path, MediaProbe& out) override;
 std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override;
 std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override;
};
}
