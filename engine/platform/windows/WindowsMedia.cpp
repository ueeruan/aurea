#include "WindowsMedia.hpp"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <mmsystem.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <thread>
#include <mutex>

namespace aurea::windows {
using Microsoft::WRL::ComPtr;
namespace {
struct Apartment { HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); ~Apartment(){ if(SUCCEEDED(hr)) CoUninitialize(); } };
void apartment(){ thread_local Apartment value; (void)value; }
Status failure(Errc code, const char* detail) { return Status{code, detail}; }
ComPtr<IMFSourceReader> reader(const char* path, bool video) {
 apartment(); ComPtr<IMFAttributes> a; MFCreateAttributes(&a, 3);
 a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
 ComPtr<IMFSourceReader> r;
 if(FAILED(MFCreateSourceReaderFromURL(wide(path).c_str(), a.Get(), &r))) return {};
 r->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
 if(FAILED(r->SetStreamSelection(video?MF_SOURCE_READER_FIRST_VIDEO_STREAM:MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE))) return {};
 return r;
}
i64 duration(IMFSourceReader* r) {
 PROPVARIANT v{}; i64 result=0;
 if(SUCCEEDED(r->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,MF_PD_DURATION,&v))) {
  if(v.vt==VT_UI8) result=static_cast<i64>(v.uhVal.QuadPart/10);
  PropVariantClear(&v);
 } return result;
}
HRESULT seek(IMFSourceReader* r, i64 us) {
 apartment(); PROPVARIANT p{}; InitPropVariantFromInt64(std::max<i64>(0,us)*10,&p);
 const auto result=r->SetCurrentPosition(GUID_NULL,p); PropVariantClear(&p); return result;
}
struct RGBFrame final : DecodedFrame {
 std::vector<u8> pixels;
 bool owns_cpu_backing() const noexcept override {
  return !pixels.empty() && planes[0] == pixels.data() && pixels.size() >= approx_bytes();
 }
};
class Video final : public VideoDecoderBackend {
 ComPtr<IMFSourceReader> r_; VideoStreamInfo info_{};
public:
 bool open(const char* path) {
  r_=reader(path,true); if(!r_) return false;
  ComPtr<IMFMediaType> native; if(FAILED(r_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&native))) return false;
  UINT32 n=30,d=1,rotation=0;
  MFGetAttributeRatio(native.Get(),MF_MT_FRAME_RATE,&n,&d);
  native->GetUINT32(MF_MT_VIDEO_ROTATION,&rotation);
  // RGB32 conversion provides SDR pixels; reject HDR rather than silently mislabelling it.
  const auto transfer=MFGetAttributeUINT32(native.Get(), MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
  if(transfer==MFVideoTransFunc_2084 || transfer==MFVideoTransFunc_HLG) return false;
  ComPtr<IMFMediaType> mt; MFCreateMediaType(&mt);
  mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video); mt->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
  if(FAILED(r_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,mt.Get()))) return false;
  if(FAILED(r_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&mt))) return false;
  MFGetAttributeSize(mt.Get(),MF_MT_FRAME_SIZE,&info_.codedWidth,&info_.codedHeight);
  info_.fps=d&&n?double(n)/d:30; info_.rotation=rotation;
  info_.durationUs=duration(r_.Get()); info_.color.fullRange=true;
  strcpy_s(info_.decoderName,"Windows Media Foundation (SDR)"); strcpy_s(info_.codec,"RGB32");
  return info_.codedWidth&&info_.codedHeight&&info_.codedWidth<=16384&&info_.codedHeight<=16384;
 }
 const VideoStreamInfo& info() const noexcept override {return info_;}
 Status seek_to_keyframe(i64 us) noexcept override {return SUCCEEDED(seek(r_.Get(),us))?OkStatus:failure(Errc::DecodeFailed,"Windows: seek do video");}
 Status next_frame(i64 from,FrameRef& out,i64& pts,bool& eos) noexcept override {
  apartment(); out.reset(); eos=false;
  DWORD flags=0; LONGLONG stamp=0; ComPtr<IMFSample> sample;
  if(FAILED(r_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&stamp,&sample))) return failure(Errc::DecodeFailed,"Windows: quadro de video");
  eos=(flags&MF_SOURCE_READERF_ENDOFSTREAM)!=0; pts=stamp/10;
  if(!sample || pts<from) return OkStatus;
  if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
   ComPtr<IMFMediaType> mt; r_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&mt);
   if(mt) MFGetAttributeSize(mt.Get(),MF_MT_FRAME_SIZE,&info_.codedWidth,&info_.codedHeight);
  }
  if(!info_.codedWidth||!info_.codedHeight||info_.codedWidth>16384||info_.codedHeight>16384) return failure(Errc::DecodeFailed,"Windows: tamanho invalido");
  ComPtr<IMFMediaBuffer> buffer; if(FAILED(sample->ConvertToContiguousBuffer(&buffer))) return failure(Errc::DecodeFailed,"Windows: buffer de video");
  BYTE* top=nullptr; LONG pitch=LONG(info_.codedWidth*4); DWORD len=0;
  ComPtr<IMF2DBuffer> two; buffer.As(&two);
  const bool locked2d=two && SUCCEEDED(two->Lock2D(&top,&pitch));
  BYTE* raw=nullptr;
  if(!locked2d) {
   if(FAILED(buffer->Lock(&raw,nullptr,&len))) return failure(Errc::DecodeFailed,"Windows: leitura do quadro");
   ComPtr<IMFMediaType> mt; r_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&mt);
   pitch=static_cast<LONG>(MFGetAttributeUINT32(mt.Get(),MF_MT_DEFAULT_STRIDE,info_.codedWidth*4));
   if(static_cast<u64>(std::abs(pitch))*info_.codedHeight>len) { buffer->Unlock(); return failure(Errc::DecodeFailed,"Windows: quadro truncado"); }
   top=pitch<0?raw+(info_.codedHeight-1)*std::abs(pitch):raw;
  }
  auto* f=new RGBFrame; f->ptsUs=pts; LONGLONG dur=0; sample->GetSampleDuration(&dur); f->durationUs=dur/10;
  f->width=f->visibleWidth=info_.codedWidth; f->height=f->visibleHeight=info_.codedHeight;
  f->rotation=info_.rotation; f->format=PixelFormat::RGBA8; f->color=info_.color;
  f->pixels.resize(size_t(f->width)*f->height*4);
  for(u32 y=0;y<f->height;++y) for(u32 x=0;x<f->width;++x) {
   const BYTE* p=top+static_cast<ptrdiff_t>(y)*pitch+x*4; u8* q=f->pixels.data()+(size_t(y)*f->width+x)*4;
   q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=255;
  }
  if(locked2d) two->Unlock2D(); else buffer->Unlock();
  f->planes[0]=f->pixels.data();f->strides[0]=f->width*4;f->planeCount=1;out=FrameRef::adopt(f);return OkStatus;
 }
};
class Audio final : public audio::AudioDecoderBackend {
 ComPtr<IMFSourceReader> r_; audio::AudioStreamInfo info_{};
public:
 bool open(const char* path) {
  r_=reader(path,false); if(!r_)return false;
  ComPtr<IMFMediaType> mt; MFCreateMediaType(&mt);mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);mt->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float);
  if(FAILED(r_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,mt.Get())))return false;
  if(FAILED(r_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&mt)))return false;
  info_.sampleRate=MFGetAttributeUINT32(mt.Get(),MF_MT_AUDIO_SAMPLES_PER_SECOND,48000);
  info_.channels=MFGetAttributeUINT32(mt.Get(),MF_MT_AUDIO_NUM_CHANNELS,2);info_.durationUs=duration(r_.Get());return true;
 }
 const audio::AudioStreamInfo& info() const noexcept override{return info_;}
 Status seek(i64 us) noexcept override {return SUCCEEDED(windows::seek(r_.Get(),us))?OkStatus:failure(Errc::DecodeFailed,"Windows: seek do audio");}
 Status read(std::vector<f32>& out,i64& pts,bool& eos) noexcept override {
  apartment();out.clear();DWORD flags=0;LONGLONG t=0;ComPtr<IMFSample> sample;
  if(FAILED(r_->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,&t,&sample)))return failure(Errc::DecodeFailed,"Windows: decode do audio");
  eos=(flags&MF_SOURCE_READERF_ENDOFSTREAM)!=0;pts=t/10;if(!sample)return OkStatus;
  ComPtr<IMFMediaBuffer> b;if(FAILED(sample->ConvertToContiguousBuffer(&b)))return failure(Errc::DecodeFailed,"Windows: buffer de audio");
  BYTE* p=nullptr;DWORD n=0;if(FAILED(b->Lock(&p,nullptr,&n)))return failure(Errc::DecodeFailed,"Windows: leitura do audio");
  out.resize(n/sizeof(float));memcpy(out.data(),p,out.size()*sizeof(float));b->Unlock();return OkStatus;
 }
};
class Sink final : public ExportSink {
 ComPtr<IMFSinkWriter> writer_; VideoStreamConfig video_{}; AudioStreamConfig audio_{};DWORD vi_=0,ai_=0;bool hasAudio_=false;std::wstring path_;
 Status sample(DWORD stream,const u8* bytes,u32 size,i64 pts,i64 dur) {
  ComPtr<IMFMediaBuffer> b;ComPtr<IMFSample> s;BYTE* p=nullptr;
  if(FAILED(MFCreateMemoryBuffer(size,&b))||FAILED(b->Lock(&p,nullptr,nullptr)))return failure(Errc::OutOfMemory,"Windows: buffer do encoder");
  memcpy(p,bytes,size);b->Unlock();b->SetCurrentLength(size);MFCreateSample(&s);s->AddBuffer(b.Get());s->SetSampleTime(pts*10);s->SetSampleDuration(dur*10);
  return SUCCEEDED(writer_->WriteSample(stream,s.Get()))?OkStatus:failure(Errc::EncodeFailed,"Windows: gravacao do MP4");
 }
public:
 Status open(const char* path,const VideoStreamConfig& v,const AudioStreamConfig* a) noexcept override {
  apartment();if(v.codec!=ExportCodec::H264)return failure(Errc::UnsupportedCodec,"Desktop Preview: exportacao H.264");
  path_=wide(path);video_=v;hasAudio_=a!=nullptr;if(a)audio_=*a;
  ComPtr<IMFAttributes> at;MFCreateAttributes(&at,2);at->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE);
  at->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE);
  if(FAILED(MFCreateSinkWriterFromURL(path_.c_str(),nullptr,at.Get(),&writer_)))return failure(Errc::IoError,"Windows: abrir destino MP4");
  auto videoType=[&](const GUID& subtype){ComPtr<IMFMediaType> mt;MFCreateMediaType(&mt);mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);mt->SetGUID(MF_MT_SUBTYPE,subtype);
   MFSetAttributeSize(mt.Get(),MF_MT_FRAME_SIZE,v.width,v.height);MFSetAttributeRatio(mt.Get(),MF_MT_FRAME_RATE,UINT32(std::llround(v.fps*1000)),1000);
   MFSetAttributeRatio(mt.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);mt->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
   mt->SetUINT32(MF_MT_VIDEO_PRIMARIES,MFVideoPrimaries_BT709);mt->SetUINT32(MF_MT_TRANSFER_FUNCTION,MFVideoTransFunc_709);
   mt->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT709);mt->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235);return mt;};
  auto output=videoType(MFVideoFormat_H264);output->SetUINT32(MF_MT_AVG_BITRATE,v.bitrateBps?v.bitrateBps:12000000);
  if(FAILED(writer_->AddStream(output.Get(),&vi_)))return failure(Errc::EncoderUnavailable,"Windows: encoder H.264 indisponivel");
  auto input=videoType(MFVideoFormat_NV12);
  if(FAILED(writer_->SetInputMediaType(vi_,input.Get(),nullptr)))return failure(Errc::EncoderUnavailable,"Windows: encoder NV12 indisponivel");
  if(a) {
   auto audioType=[&](const GUID& sub){ComPtr<IMFMediaType> mt;MFCreateMediaType(&mt);mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);mt->SetGUID(MF_MT_SUBTYPE,sub);mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,a->channels);mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,a->sampleRate);mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);return mt;};
   auto ao=audioType(MFAudioFormat_AAC);ao->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,a->bitrateBps/8);ao->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE,0);
   if(FAILED(writer_->AddStream(ao.Get(),&ai_)))return failure(Errc::EncoderUnavailable,"Windows: encoder AAC indisponivel");
   auto in=audioType(MFAudioFormat_PCM);in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,a->channels*2);in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,a->sampleRate*a->channels*2);
   if(FAILED(writer_->SetInputMediaType(ai_,in.Get(),nullptr)))return failure(Errc::EncoderUnavailable,"Windows: entrada PCM indisponivel");
  }
  return SUCCEEDED(writer_->BeginWriting())?OkStatus:failure(Errc::EncodeFailed,"Windows: iniciar MP4");
 }
 Status write_video(const u8* y,u32 ys,const u8* uv,u32 uvs,i64 pts) noexcept override {
  apartment();const u32 w=video_.width,h=video_.height;std::vector<u8> data(size_t(w)*h*3/2);
  for(u32 row=0;row<h;++row)memcpy(data.data()+size_t(row)*w,y+size_t(row)*ys,w);
  for(u32 row=0;row<h/2;++row)memcpy(data.data()+size_t(w)*h+size_t(row)*w,uv+size_t(row)*uvs,w);
  return sample(vi_,data.data(),u32(data.size()),pts,i64(std::llround(1000000/video_.fps)));
 }
 Status write_audio(const i16* data,u32 frames,i64 pts) noexcept override {
  apartment();return hasAudio_?sample(ai_,reinterpret_cast<const u8*>(data),frames*audio_.channels*2,pts,i64(frames)*1000000/audio_.sampleRate):OkStatus;
 }
 Status finish() noexcept override {apartment();const auto hr=writer_?writer_->Finalize():E_FAIL;writer_.Reset();return SUCCEEDED(hr)?OkStatus:failure(Errc::EncodeFailed,"Windows: finalizar MP4");}
 void abort() noexcept override {writer_.Reset();if(!path_.empty())DeleteFileW(path_.c_str());}
 EncoderInfo encoder_info() const noexcept override {EncoderInfo out;strcpy_s(out.name,"Media Foundation H.264");return out;}
};
class Sound final : public audio::AudioOutput {
 HWAVEOUT device_=nullptr;HANDLE event_=nullptr;audio::AudioRenderFn fn_=nullptr;void* ctx_=nullptr;
 std::atomic<bool> running_{false};std::thread worker_;std::mutex mutex_;i64 base_=0;
public:
 ~Sound() override {close();}
 Status open(audio::AudioRenderFn fn,void* ctx) noexcept override {
  close();fn_=fn;ctx_=ctx;event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  WAVEFORMATEX fmt{WAVE_FORMAT_IEEE_FLOAT,2,48000,48000*8,8,32,0};
  if(waveOutOpen(&device_,WAVE_MAPPER,&fmt,reinterpret_cast<DWORD_PTR>(event_),0,CALLBACK_EVENT)!=MMSYSERR_NOERROR){close();return failure(Errc::NotSupported,"Windows: saida de audio indisponivel");}return OkStatus;
 }
 Status start() noexcept override {
  if(running_.exchange(true))return OkStatus;
  if(!device_){running_=false;return failure(Errc::InvalidState,"Windows: audio fechado");}
  worker_=std::thread([this]{
   std::array<std::array<float,960>,3> pcm{};std::array<WAVEHDR,3> heads{};
   for(size_t i=0;i<heads.size();++i){heads[i].lpData=reinterpret_cast<LPSTR>(pcm[i].data());heads[i].dwBufferLength=sizeof(pcm[i]);waveOutPrepareHeader(device_,&heads[i],sizeof(WAVEHDR));}
   while(running_){for(auto& h:heads)if(!(h.dwFlags&WHDR_INQUEUE)){fn_(ctx_,reinterpret_cast<float*>(h.lpData),480);waveOutWrite(device_,&h,sizeof(h));}WaitForSingleObject(event_,10);}
   {std::lock_guard<std::mutex> guard(mutex_);MMTIME time{};time.wType=TIME_SAMPLES;if(waveOutGetPosition(device_,&time,sizeof(time))==0&&time.wType==TIME_SAMPLES)base_+=time.u.sample;waveOutReset(device_);}
   for(auto& h:heads)waveOutUnprepareHeader(device_,&h,sizeof(h));
  });return OkStatus;
 }
 void stop() noexcept override {running_=false;if(event_)SetEvent(event_);if(worker_.joinable())worker_.join();}
 void close() noexcept override {stop();if(device_){waveOutClose(device_);device_=nullptr;}if(event_){CloseHandle(event_);event_=nullptr;}}
 bool presented(u64,i64& frames) noexcept override {std::lock_guard<std::mutex> guard(mutex_);if(!device_)return false;MMTIME t{};t.wType=TIME_SAMPLES;if(waveOutGetPosition(device_,&t,sizeof(t))!=0||t.wType!=TIME_SAMPLES)return false;frames=base_+t.u.sample;return true;}
 u32 latency_frames() const noexcept override {return 1440;}
};
}
std::wstring wide(const std::string& text){if(text.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),out.data(),n);return out;}
std::string utf8(const std::wstring& text){if(text.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),n,nullptr,nullptr);return out;}
bool initialize_media(){apartment();return SUCCEEDED(MFStartup(MF_VERSION));}
void shutdown_media(){MFShutdown();}
bool load_image(const char* path,ImagePixels& out,void*) {
 apartment();ComPtr<IWICImagingFactory> wic;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
 if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))||FAILED(wic->CreateDecoderFromFilename(wide(path).c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))||FAILED(decoder->GetFrame(0,&frame)))return false;
 UINT w=0,h=0;frame->GetSize(&w,&h);if(!w||!h||u64(w)*h>67108864)return false;
 if(FAILED(wic->CreateFormatConverter(&converter))||FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
 out.width=w;out.height=h;out.rgba.resize(size_t(w)*h*4);return SUCCEEDED(converter->CopyPixels(nullptr,w*4,UINT(out.rgba.size()),out.rgba.data()));
}
bool save_png(const std::wstring& path,const u8* rgba,u32 w,u32 h) {
 apartment();ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> enc;ComPtr<IWICBitmapFrameEncode> frame;
 if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&enc))||FAILED(enc->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(enc->CreateNewFrame(&frame,nullptr))||FAILED(frame->Initialize(nullptr)))return false;
 frame->SetSize(w,h);WICPixelFormatGUID format=GUID_WICPixelFormat32bppRGBA;frame->SetPixelFormat(&format);
 return SUCCEEDED(frame->WritePixels(h,w*4,w*h*4,const_cast<BYTE*>(rgba)))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(enc->Commit());
}
bool MediaFactory::probe(const char* path,MediaProbe& out){out={};Video v;Audio a;if(v.open(path)){out.hasVideo=true;out.video=v.info();}if(a.open(path)){out.hasAudio=true;out.audioSampleRate=a.info().sampleRate;out.audioChannels=a.info().channels;out.audioDurationUs=a.info().durationUs;}return out.hasVideo||out.hasAudio;}
std::unique_ptr<VideoDecoderBackend> MediaFactory::open_video(const Asset& asset,MediaPriority){auto v=std::make_unique<Video>();return v->open(asset.sourcePath.c_str())?std::move(v):nullptr;}
std::unique_ptr<audio::AudioDecoderBackend> MediaFactory::open_audio(const char* path){auto a=std::make_unique<Audio>();return a->open(path)?std::move(a):nullptr;}
std::unique_ptr<ExportSink> make_export_sink(void*){return std::make_unique<Sink>();}
std::unique_ptr<audio::AudioOutput> make_audio_output(){return std::make_unique<Sound>();}
}
