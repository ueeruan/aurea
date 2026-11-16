#include "WindowsMedia.hpp"
#include "aurea/core/Log.hpp"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <thread>
#include <mutex>
#include <vector>

// Os dois alvos que compilam este arquivo (aurea.dll e o editor nativo) pegam
// as bibliotecas daqui: WASAPI/MMCSS e o dispositivo D3D11 do DXVA.
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "mfplat.lib")

namespace aurea::windows {
using Microsoft::WRL::ComPtr;
namespace {
struct Apartment { HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); ~Apartment(){ if(SUCCEEDED(hr)) CoUninitialize(); } };
void apartment(){ thread_local Apartment value; (void)value; }
Status failure(Errc code, const char* detail) { return Status{code, detail}; }

// -----------------------------------------------------------------------------
// Decode de hardware (DXVA). Um dispositivo D3D11 por processo, na placa com
// mais memória dedicada (a mesma que o Vulkan escolhe), protegido para várias
// threads de decode. AUREA_MF_SOFTWARE=1 desliga (diagnóstico).
// -----------------------------------------------------------------------------
struct Dxva { ComPtr<ID3D11Device> device; ComPtr<IMFDXGIDeviceManager> manager; UINT token = 0; };
Dxva* dxva() {
 static Dxva* value = []() -> Dxva* {
  char off[8]{};
  if(GetEnvironmentVariableA("AUREA_MF_SOFTWARE",off,sizeof(off)) && off[0]=='1') return nullptr;
  ComPtr<IDXGIFactory1> factory; if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;
  ComPtr<IDXGIAdapter1> best, a; SIZE_T bestMem=0;
  for(UINT i=0; factory->EnumAdapters1(i,&a)!=DXGI_ERROR_NOT_FOUND; ++i) {
   DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
   if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && (!best||d.DedicatedVideoMemory>bestMem)){best=a;bestMem=d.DedicatedVideoMemory;}
  }
  auto* v=new Dxva;
  const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
  const UINT flags=D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  HRESULT hr=D3D11CreateDevice(best.Get(),best?D3D_DRIVER_TYPE_UNKNOWN:D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,levels,4,D3D11_SDK_VERSION,&v->device,nullptr,nullptr);
  if(FAILED(hr)){delete v;AUREA_LOG_WARN("Media Foundation: sem D3D11 de video (0x%08lx); decode por software",hr);return nullptr;}
  ComPtr<ID3D10Multithread> mt; if(SUCCEEDED(v->device.As(&mt))) mt->SetMultithreadProtected(TRUE);
  if(FAILED(MFCreateDXGIDeviceManager(&v->token,&v->manager))||FAILED(v->manager->ResetDevice(v->device.Get(),v->token))){delete v;return nullptr;}
  return v;
 }();
 return value;
}
ComPtr<IMFSourceReader> reader(const char* path, bool video, bool hardware=false) {
 if(!path||!*path)return {};
 apartment(); ComPtr<IMFAttributes> a;
 if(FAILED(MFCreateAttributes(&a, 4)))return {};
 if(video) {
  // Com o gerente D3D o decoder usa DXVA e entrega texturas; sem ele (ou se o
  // perfil não tem hardware) o MF cai sozinho no decoder de software.
  Dxva* d=hardware?dxva():nullptr;
  if(d){a->SetUnknown(MF_SOURCE_READER_D3D_MANAGER,d->manager.Get());a->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE);}
 }
 ComPtr<IMFSourceReader> r;
 if(FAILED(MFCreateSourceReaderFromURL(wide(path).c_str(), a.Get(), &r))) return {};
 r->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
 if(FAILED(r->SetStreamSelection(video?MF_SOURCE_READER_FIRST_VIDEO_STREAM:MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE))) return {};
 return r;
}
ComPtr<IMFSourceReader> rgb_reader(const char* path) {
 // Caminho antigo (RGB32 convertido pelo MF): só quando nenhum YUV negocia.
 apartment(); ComPtr<IMFAttributes> a;
 if(FAILED(MFCreateAttributes(&a, 1))||FAILED(a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)))return {};
 ComPtr<IMFSourceReader> r;
 if(FAILED(MFCreateSourceReaderFromURL(wide(path).c_str(), a.Get(), &r))) return {};
 r->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
 if(FAILED(r->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE))) return {};
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
// Memória dos quadros YUV reciclada: 4K NV12 são 12 MB; alocar do zero a cada
// quadro pagaria as faltas de página do sistema toda vez.
struct BufferPool {
 std::mutex mutex; std::vector<std::unique_ptr<u8[]>> free; size_t bytes=0;
 std::unique_ptr<u8[]> take(size_t n) {
  { std::lock_guard<std::mutex> g(mutex);
    if(n!=bytes){free.clear();bytes=n;}
    if(!free.empty()){auto b=std::move(free.back());free.pop_back();return b;} }
  return std::unique_ptr<u8[]>(new (std::nothrow) u8[n]);
 }
 void give(std::unique_ptr<u8[]> b, size_t n) {
  std::lock_guard<std::mutex> g(mutex);
  if(n==bytes&&free.size()<12) free.push_back(std::move(b));
 }
};
struct YuvFrame final : DecodedFrame {
 std::shared_ptr<BufferPool> pool; std::unique_ptr<u8[]> data; size_t bytes=0;
 ~YuvFrame() override { if(pool&&data) pool->give(std::move(data),bytes); }
 bool owns_cpu_backing() const noexcept override { return data && planes[0]==data.get(); }
};
void color_from(IMFMediaType* t, VideoColorInfo& c, bool& any) {
 UINT32 v=0;
 if(SUCCEEDED(t->GetUINT32(MF_MT_YUV_MATRIX,&v))) {
  any=true;
  if(v==MFVideoTransferMatrix_BT601||v==MFVideoTransferMatrix_SMPTE240M) c.matrix=YCbCrMatrix::BT601;
  else if(v==MFVideoTransferMatrix_BT2020_10||v==MFVideoTransferMatrix_BT2020_12) c.matrix=YCbCrMatrix::BT2020;
  else if(v==MFVideoTransferMatrix_BT709) c.matrix=YCbCrMatrix::BT709;
 }
 if(SUCCEEDED(t->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,&v))) { any=true; c.fullRange=v==MFNominalRange_0_255; }
 if(SUCCEEDED(t->GetUINT32(MF_MT_VIDEO_PRIMARIES,&v))) {
  any=true;
  if(v==MFVideoPrimaries_BT2020) c.primaries=ColorPrimaries::BT2020;
  else if(v==MFVideoPrimaries_DCI_P3) c.primaries=ColorPrimaries::P3;
  else if(v==MFVideoPrimaries_SMPTE170M||v==MFVideoPrimaries_BT470_2_SysBG||v==MFVideoPrimaries_BT470_2_SysM) c.primaries=ColorPrimaries::BT601;
  else if(v==MFVideoPrimaries_BT709) c.primaries=ColorPrimaries::BT709;
 }
 if(SUCCEEDED(t->GetUINT32(MF_MT_TRANSFER_FUNCTION,&v))) {
  any=true;
  if(v==MFVideoTransFunc_2084) c.transfer=TransferFunction::PQ;
  else if(v==MFVideoTransFunc_HLG) c.transfer=TransferFunction::HLG;
  else if(v==MFVideoTransFunc_10) c.transfer=TransferFunction::Linear;
  else c.transfer=TransferFunction::SRGB;
 }
}
f64 ms_since(LARGE_INTEGER t0) {
 LARGE_INTEGER t1{},f{}; QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f);
 return static_cast<f64>(t1.QuadPart-t0.QuadPart)*1000.0/static_cast<f64>(f.QuadPart);
}
LARGE_INTEGER qpc_now() { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t; }
class Video final : public VideoDecoderBackend {
public:
 VideoDecodeTimings timings_{};
private:
 ComPtr<IMFSourceReader> r_; VideoStreamInfo info_{};
 PixelFormat format_=PixelFormat::RGBA8; LONG defaultStride_=0; bool checkedHardware_=false;
 std::shared_ptr<BufferPool> pool_=std::make_shared<BufferPool>();
 // Tamanho codificado, recorte visível e passo padrão do tipo de saída atual.
 void read_output_type() {
  ComPtr<IMFMediaType> mt; if(FAILED(r_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&mt))) return;
  MFGetAttributeSize(mt.Get(),MF_MT_FRAME_SIZE,&info_.codedWidth,&info_.codedHeight);
  defaultStride_=static_cast<LONG>(MFGetAttributeUINT32(mt.Get(),MF_MT_DEFAULT_STRIDE,0));
  MFVideoArea area{}; UINT32 got=0;
  info_.cropLeft=info_.cropTop=info_.cropRight=info_.cropBottom=0;
  if(SUCCEEDED(mt->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,reinterpret_cast<UINT8*>(&area),sizeof(area),&got))&&got==sizeof(area)
     &&area.Area.cx>0&&area.Area.cy>0) {
   const u32 x=static_cast<u32>(std::max<i32>(0,area.OffsetX.value)), y=static_cast<u32>(std::max<i32>(0,area.OffsetY.value));
   if(x+area.Area.cx<=info_.codedWidth&&y+area.Area.cy<=info_.codedHeight&&(area.Area.cx!=LONG(info_.codedWidth)||area.Area.cy!=LONG(info_.codedHeight))) {
    info_.cropLeft=x; info_.cropTop=y; info_.cropRight=x+area.Area.cx-1; info_.cropBottom=y+area.Area.cy-1;
   }
  }
 }
 void describe_decoder() {
  ComPtr<IMFSourceReaderEx> ex; GUID category{}; ComPtr<IMFTransform> mft;
  if(FAILED(r_.As(&ex))||FAILED(ex->GetTransformForStream(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&category,&mft))||!mft) return;
  ComPtr<IMFAttributes> a; if(FAILED(mft->GetAttributes(&a))||!a) return;
  WCHAR name[64]{}; UINT32 len=0;
  if(SUCCEEDED(a->GetString(MFT_FRIENDLY_NAME_Attribute,name,64,&len))) {
   const std::string n=utf8(name); strncpy_s(info_.decoderName,n.c_str(),_TRUNCATE);
  }
  UINT32 aware=0, urlLen=0;
  // Com o gerente D3D: decoder ciente do D3D11 usa DXVA (confirmado no 1º quadro).
  info_.hardwareDecoder=(SUCCEEDED(a->GetUINT32(MF_SA_D3D11_AWARE,&aware))&&aware&&dxva())
                      || SUCCEEDED(a->GetStringLength(MFT_ENUM_HARDWARE_URL_Attribute,&urlLen));
 }
 bool open_rgb(const char* path) {
  r_=rgb_reader(path); if(!r_) return false;
  ComPtr<IMFMediaType> mt; MFCreateMediaType(&mt);
  mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video); mt->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
  if(FAILED(r_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,mt.Get()))) return false;
  format_=PixelFormat::RGBA8; read_output_type();
  info_.cropLeft=info_.cropTop=info_.cropRight=info_.cropBottom=0;
  info_.color=VideoColorInfo{}; info_.color.fullRange=true; info_.hardwareDecoder=false;
  strcpy_s(info_.decoderName,"Windows Media Foundation (RGB32)");
  return true;
 }
public:
 bool open(const char* path, bool hardware) {
  r_=reader(path,true,hardware); if(!r_) return false;
  // AUREA_MF_LOCK2D=1: sem o anel, a textura lida pelo Lock2D do MF (diagnóstico).
  char lock2d[4]{}; stagingOff_=GetEnvironmentVariableA("AUREA_MF_LOCK2D",lock2d,sizeof(lock2d))&&lock2d[0]=='1';
  ComPtr<IMFMediaType> native; if(FAILED(r_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&native))) return false;
  UINT32 n=30,d=1,rotation=0;
  MFGetAttributeRatio(native.Get(),MF_MT_FRAME_RATE,&n,&d);
  native->GetUINT32(MF_MT_VIDEO_ROTATION,&rotation);
  GUID sub{}; native->GetGUID(MF_MT_SUBTYPE,&sub);
  const UINT32 profile=MFGetAttributeUINT32(native.Get(),MF_MT_MPEG2_PROFILE,0);
  const char* codec="video/mf";
  if(sub==MFVideoFormat_H264) codec="video/avc"; else if(sub==MFVideoFormat_HEVC) codec="video/hevc";
  else if(sub==MFVideoFormat_VP90) codec="video/x-vnd.on2.vp9"; else if(sub==MFVideoFormat_AV1) codec="video/av01";
  strcpy_s(info_.codec,codec);
  bool fromStream=false; VideoColorInfo color{}; color_from(native.Get(),color,fromStream);
  // 10 bits: HEVC Main10 (perfil 2), VP9 perfil 2/3, ou HDR declarado.
  const bool tenBit=(sub==MFVideoFormat_HEVC&&profile==2)||(sub==MFVideoFormat_VP90&&profile>=2)||color.hdr();
  // NV12/P010 direto do decoder: nenhuma conversão por pixel na CPU — os
  // planos sobem para a GPU e o shader converte com a cor do arquivo.
  const GUID tries[2]={tenBit?MFVideoFormat_P010:MFVideoFormat_NV12,tenBit?MFVideoFormat_NV12:GUID_NULL};
  bool yuv=false;
  for(const GUID& want:tries) {
   if(want==GUID_NULL) break;
   ComPtr<IMFMediaType> mt; MFCreateMediaType(&mt);
   mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video); mt->SetGUID(MF_MT_SUBTYPE,want);
   if(SUCCEEDED(r_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,mt.Get()))) {
    format_=want==MFVideoFormat_P010?PixelFormat::P010:PixelFormat::NV12; yuv=true; break;
   }
  }
  if(yuv) {
   read_output_type();
   ComPtr<IMFMediaType> cur; if(SUCCEEDED(r_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&cur))) color_from(cur.Get(),color,fromStream);
   const u8 depth=format_==PixelFormat::P010?10:8;
   if(!fromStream) color=VideoColorInfo::guess(info_.visible_width(),info_.visible_height(),depth);
   color.bitDepth=depth; color.fromStream=fromStream; info_.color=color;
   strcpy_s(info_.decoderName,"Windows Media Foundation");
   describe_decoder();
  } else {
   // HDR em RGB32 sairia com a cor errada: melhor recusar.
   if(color.hdr()) return false;
   r_.Reset(); if(!open_rgb(path)) return false;
  }
  info_.fps=d&&n?double(n)/d:30; info_.rotation=rotation;
  info_.preciseFrameTiming=true;   // durações reais pelo quadro adiantado (next_frame)
  info_.durationUs=duration(r_.Get());
  return info_.codedWidth&&info_.codedHeight&&info_.codedWidth<=16384&&info_.codedHeight<=16384;
 }
 const VideoStreamInfo& info() const noexcept override {return info_;}
 // 0.5.3: os quadros daqui são cópias próprias na memória (BufferPool), sem o
 // limite de buffers de um decoder de hardware. Com o padrão (8) o VideoSource
 // guardava só 3 quadros e 1 para trás; 13 dá 8 em cache e 6 para trás (o iOS
 // usa 12). O teto em bytes do DecodedFrameCache e o orçamento do motor seguem valendo.
 u32 max_live_frames() const noexcept override { return 13; }
 Status seek_to_keyframe(i64 us) noexcept override {
  // O que estava adiantado é do ponto antigo.
  clear_ahead(); seeking_=true;
  return SUCCEEDED(seek(r_.Get(),us))?OkStatus:failure(Errc::DecodeFailed,"Windows: seek do video");
 }
 // Como o MediaCodec/VideoToolbox (preciseFrameTiming): um quadro adiantado
 // dá a duração REAL de cada quadro (próximo pts − pts). A duração da amostra
 // do MF é a nominal; em gravação de fps variável (ShadowPlay, celular) ela
 // deixava buracos e sobreposições, o alvo da grade nunca ficava "exato" e o
 // quadro que cobre o alvo (pts logo antes dele) era descartado: a prévia
 // parada ficava preta e a captura esperava até o prazo.
 // Com DXVA a fila vai mais fundo (kAhead): a cópia de cada textura para a
 // memória sai assim que a amostra chega e só é lida quadros depois.
 Status next_frame(i64 from,FrameRef& out,i64& pts,bool& eos) noexcept override {
  apartment(); out.reset(); eos=false;
  if(const Status s=fill(); !s.ok()) return s;
  if(ahead_.empty()) { eos=true; pts=lastPts_; return OkStatus; }
  Ahead current=std::move(ahead_.front()); ahead_.pop_front(); pts=current.pts;
  const i64 duration=!ahead_.empty()?ahead_.front().pts-pts:info_.durationUs>pts?info_.durationUs-pts:std::max<i64>(1,static_cast<i64>(1e6/std::max(1.0,info_.fps)));
  eos=aheadEos_&&ahead_.empty();
  if(current.typeChanged) read_output_type();
  // Entregue a partir de `from`, e também o quadro que COBRE `from`.
  if(pts<from&&pts+duration<=from) { free_slot(current.slot); return OkStatus; }
  if(!info_.codedWidth||!info_.codedHeight||info_.codedWidth>16384||info_.codedHeight>16384) { free_slot(current.slot); return failure(Errc::DecodeFailed,"Windows: tamanho invalido"); }
  const Status s=format_==PixelFormat::RGBA8?rgb_frame(current.sample.Get(),pts,out):yuv_frame(current,pts,out);
  if(s.ok()&&out) { out->durationUs=duration; seeking_=false; }
  return s;
 }
private:
 // Amostra adiantada: ou a textura já copiada para uma de leitura (slot), ou
 // a própria amostra do MF (software, ou a cópia própria não deu).
 struct Ahead { ComPtr<IMFSample> sample; int slot=-1; i64 pts=0; bool typeChanged=false; };
 static constexpr size_t kAhead=3;
 std::deque<Ahead> ahead_; bool aheadEos_=false, seeking_=false; i64 lastPts_=0;
 // Texturas de leitura (D3D11_USAGE_STAGING) do anel. Antes, o Lock2D do MF
 // copiava e ESPERAVA a GPU a cada quadro: ~7 ms parado e dezenas com o
 // Vulkan ocupando a placa (o detector de decoder lento pedia proxy).
 struct Slot { ComPtr<ID3D11Texture2D> tex; u64 fence=0; bool busy=false; };
 std::vector<Slot> slots_; D3D11_TEXTURE2D_DESC slotDesc_{}; bool stagingOff_=false;
 ComPtr<ID3D11DeviceContext> context_;
 // Cerca do D3D11.4: a espera pela cópia acorda por evento (o Sleep(1) do
 // Windows dorme 15,6 ms sem timeBeginPeriod). Sem ela, espera em giro.
 ComPtr<ID3D11DeviceContext4> context4_; ComPtr<ID3D11Fence> fence_; u64 fenceValue_=0; HANDLE fenceEvent_=nullptr;
 void open_context(ID3D11Texture2D* src) noexcept {
  ComPtr<ID3D11Device> dev; src->GetDevice(&dev); dev->GetImmediateContext(&context_);
  ComPtr<ID3D11Device5> dev5;
  if(SUCCEEDED(dev.As(&dev5))&&SUCCEEDED(context_.As(&context4_))&&SUCCEEDED(dev5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_))))
   fenceEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  if(!fenceEvent_) { fence_.Reset(); context4_.Reset(); }
 }
 public: ~Video() override { if(fenceEvent_) CloseHandle(fenceEvent_); }
 private:
 void free_slot(int slot) noexcept { if(slot>=0&&size_t(slot)<slots_.size()) slots_[size_t(slot)].busy=false; }
 void clear_ahead() noexcept { for(Ahead& a:ahead_) free_slot(a.slot); ahead_.clear(); aheadEos_=false; }
 // Copia a textura do decoder para uma de leitura (só comandos na GPU, sem
 // esperar) e solta a amostra: a superfície volta ao decoder na hora.
 void stage(Ahead& a) noexcept {
  if(stagingOff_||!a.sample||format_==PixelFormat::RGBA8) return;
  ComPtr<IMFMediaBuffer> buffer; ComPtr<IMFDXGIBuffer> gpu; ComPtr<ID3D11Texture2D> src; UINT sub=0;
  if(FAILED(a.sample->GetBufferByIndex(0,&buffer))||FAILED(buffer.As(&gpu))) { stagingOff_=true; return; }   // software: memória
  if(FAILED(gpu->GetResource(IID_PPV_ARGS(&src)))||FAILED(gpu->GetSubresourceIndex(&sub))) { stagingOff_=true; return; }
  D3D11_TEXTURE2D_DESC d{}; src->GetDesc(&d);
  if(!context_) open_context(src.Get());
  if(d.Width!=slotDesc_.Width||d.Height!=slotDesc_.Height||d.Format!=slotDesc_.Format) {
   for(const Slot& s:slots_) if(s.busy) return;   // troca de tamanho com quadros na fila: este vai pelo MF
   slots_.clear(); slotDesc_=d;
  }
  if(d.Width<info_.codedWidth||d.Height<info_.codedHeight) { stagingOff_=true; return; }   // inesperado: tudo pelo MF
  size_t i=0; while(i<slots_.size()&&slots_[i].busy) ++i;
  if(i==slots_.size()) {
   if(slots_.size()>kAhead+1) return;
   D3D11_TEXTURE2D_DESC sd{}; sd.Width=d.Width; sd.Height=d.Height; sd.MipLevels=1; sd.ArraySize=1; sd.Format=d.Format;
   sd.SampleDesc.Count=1; sd.Usage=D3D11_USAGE_STAGING; sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
   ComPtr<ID3D11Device> dev; src->GetDevice(&dev); Slot slot;
   if(FAILED(dev->CreateTexture2D(&sd,nullptr,&slot.tex))) { stagingOff_=true; AUREA_LOG_WARN("Media Foundation: sem textura de leitura; Lock2D do MF"); return; }
   slots_.push_back(std::move(slot));
  }
  context_->CopySubresourceRegion(slots_[i].tex.Get(),0,0,0,0,src.Get(),sub,nullptr);
  slots_[i].fence=0;
  if(context4_&&SUCCEEDED(context4_->Signal(fence_.Get(),fenceValue_+1))) slots_[i].fence=++fenceValue_;
  context_->Flush();
  slots_[i].busy=true; a.slot=static_cast<int>(i); a.sample.Reset();
 }
 // Enche a fila: kAhead+1 com DXVA, senão só o quadro e o seguinte (a duração).
 // Até o 1º quadro depois de um seek também só dois: o alvo sai sem esperar
 // o decode de mais kAhead quadros.
 Status fill() noexcept {
  const size_t want=stagingOff_||seeking_||format_==PixelFormat::RGBA8?2:kAhead+1;
  bool carry=false;   // troca de tipo vista numa amostra descartada
  for(u32 dropped=0; ahead_.size()<want&&!aheadEos_;) {
   Ahead a; bool end=false;
   if(const Status s=read_raw(a.sample,a.pts,end,a.typeChanged); !s.ok()) return s;
   if(end) aheadEos_=true;
   if(!a.sample) break;
   a.typeChanged=a.typeChanged||carry; carry=false;
   // PTS repetido/para trás (remux ruim, gravador de tela): o repetido sai.
   if(!ahead_.empty()&&a.pts<=ahead_.back().pts) {
    if(++dropped>=64) return failure(Errc::DecodeFailed,"Windows: PTS de video fora de ordem");
    carry=a.typeChanged; continue;
   }
   stage(a);
   ahead_.push_back(std::move(a));
  }
  return OkStatus;
 }
 // Uma amostra de verdade (o MF também devolve só sinais: tick, troca de tipo).
 Status read_raw(ComPtr<IMFSample>& sample,i64& pts,bool& eos,bool& typeChanged) noexcept {
  sample.Reset(); eos=false; typeChanged=false;
  for(u32 tries=0; tries<256; ++tries) {
   DWORD flags=0; LONGLONG stamp=0;
   const LARGE_INTEGER t0=qpc_now();
   const HRESULT hr=r_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&stamp,&sample);
   timings_.readMs+=ms_since(t0);
   if(FAILED(hr)) return failure(Errc::DecodeFailed,"Windows: quadro de video");
   typeChanged=typeChanged||(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)!=0;
   if(sample) { ++timings_.samples; pts=lastPts_=stamp/10; eos=(flags&MF_SOURCE_READERF_ENDOFSTREAM)!=0; return OkStatus; }
   if(flags&(MF_SOURCE_READERF_ENDOFSTREAM|MF_SOURCE_READERF_ERROR)) { eos=true; pts=lastPts_; return OkStatus; }
  }
  return failure(Errc::DecodeFailed,"Windows: decoder sem quadro");
 }
 // Lê a textura do anel. Quase sempre a GPU já terminou (a cópia saiu
 // kAhead quadros antes); se não, espera sem segurar o contexto do D3D11,
 // que os outros decoders dividem.
 bool map_slot(int slot,D3D11_MAPPED_SUBRESOURCE& m) noexcept {
  ID3D11Texture2D* tex=slots_[size_t(slot)].tex.Get();
  const u64 wait=slots_[size_t(slot)].fence;
  if(wait&&fence_->GetCompletedValue()<wait&&SUCCEEDED(fence_->SetEventOnCompletion(wait,fenceEvent_))) WaitForSingleObject(fenceEvent_,4000);
  for(u32 spins=0;;++spins) {
   const HRESULT hr=context_->Map(tex,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&m);
   if(hr!=DXGI_ERROR_WAS_STILL_DRAWING) return SUCCEEDED(hr);
   if(spins>=4000) return SUCCEEDED(context_->Map(tex,0,D3D11_MAP_READ,0,&m));   // ~4 s: espera de vez
   if(spins<256) SwitchToThread(); else Sleep(1);
  }
 }
 Status yuv_frame(Ahead& a,i64 pts,FrameRef& out) noexcept {
  const u32 w=info_.codedWidth, h=info_.codedHeight, bpp=format_==PixelFormat::P010?2:1;
  const u32 cw=(w+1)/2, ch=(h+1)/2;
  const LARGE_INTEGER tMap=qpc_now();
  BYTE* y=nullptr; LONG pitch=0; u32 rows=h;   // linhas do plano Y no buffer (o CbCr vem depois)
  ComPtr<IMFMediaBuffer> buffer; ComPtr<IMF2DBuffer2> two2; ComPtr<IMF2DBuffer> two; bool locked2d=false, mapped=false;
  if(a.slot>=0) {
   // Textura do anel: a cópia da GPU saiu quadros antes.
   if(!checkedHardware_) { info_.hardwareDecoder=true; checkedHardware_=true; timings_.path="DXVA, anel de texturas de leitura"; }
   D3D11_MAPPED_SUBRESOURCE m{};
   if(!map_slot(a.slot,m)) { free_slot(a.slot); a.slot=-1; return failure(Errc::DecodeFailed,"Windows: leitura da textura do quadro"); }
   mapped=true; y=static_cast<BYTE*>(m.pData); pitch=static_cast<LONG>(m.RowPitch); rows=slotDesc_.Height;
  } else {
   IMFSample* sample=a.sample.Get();
   if(!sample) return failure(Errc::DecodeFailed,"Windows: amostra de video ausente");
   DWORD count=0; sample->GetBufferCount(&count);
   if(FAILED(count==1?sample->GetBufferByIndex(0,&buffer):sample->ConvertToContiguousBuffer(&buffer))) return failure(Errc::DecodeFailed,"Windows: buffer de video");
   if(!checkedHardware_) {
    // A verdade do DXVA: o quadro veio numa textura da GPU.
    ComPtr<IMFDXGIBuffer> gpu; info_.hardwareDecoder=SUCCEEDED(buffer.As(&gpu)); checkedHardware_=true;
    timings_.path=info_.hardwareDecoder?"DXVA, textura lida pelo MF (Lock2D)":"software, memoria do MF";
   }
   if(SUCCEEDED(buffer.As(&two2))) {
    // Textura DXVA sem o anel: o MF copia da GPU para uma área de leitura e
    // diz o tamanho, que dá a altura real da superfície.
    BYTE* start=nullptr; DWORD len=0;
    if(SUCCEEDED(two2->Lock2DSize(MF2DBuffer_LockFlags_Read,&y,&pitch,&start,&len))) {
     locked2d=true;
     if(pitch>0) { const u64 total=(static_cast<u64>(len)-static_cast<u64>(y-start))/static_cast<u64>(pitch); rows=static_cast<u32>(std::max<u64>(h,total*2/3)); }
    }
   } else if(SUCCEEDED(buffer.As(&two))&&SUCCEEDED(two->Lock2D(&y,&pitch))) locked2d=true;
   if(!locked2d) {
    BYTE* raw=nullptr; DWORD len=0;
    if(FAILED(buffer->Lock(&raw,nullptr,&len))) return failure(Errc::DecodeFailed,"Windows: leitura do quadro");
    y=raw; pitch=defaultStride_>0?defaultStride_:LONG(w*bpp);
    if(static_cast<u64>(pitch)*h*3/2>len){buffer->Unlock();return failure(Errc::DecodeFailed,"Windows: quadro truncado");}
   }
  }
  const auto unlock=[&]{
   if(mapped){ context_->Unmap(slots_[size_t(a.slot)].tex.Get(),0); free_slot(a.slot); a.slot=-1; }
   else if(locked2d){ if(two2) two2->Unlock2D(); else two->Unlock2D(); } else buffer->Unlock();
  };
  if(pitch<LONG(w*bpp)){unlock();return failure(Errc::DecodeFailed,"Windows: passo de linha invalido");}
  const size_t yRow=size_t(w)*bpp, uvRow=size_t(cw)*2*bpp, bytes=yRow*h+uvRow*ch;
  auto* f=new (std::nothrow) YuvFrame;
  if(!f){unlock();return failure(Errc::OutOfMemory,"Windows: quadro");}
  f->pool=pool_; f->bytes=bytes; f->data=pool_->take(bytes);
  if(!f->data){unlock();f->release();return failure(Errc::OutOfMemory,"Windows: quadro");}
  timings_.mapMs+=ms_since(tMap); const LARGE_INTEGER tCopy=qpc_now();
  // Cópia única, linha a linha, sem tocar nos pixels.
  u8* dst=f->data.get(); const BYTE* uv=y+static_cast<size_t>(pitch)*rows;
  if(size_t(pitch)==yRow) std::memcpy(dst,y,yRow*h); else for(u32 r=0;r<h;++r) std::memcpy(dst+r*yRow,y+size_t(r)*pitch,yRow);
  u8* dstUv=dst+yRow*h;
  if(size_t(pitch)==uvRow) std::memcpy(dstUv,uv,uvRow*ch); else for(u32 r=0;r<ch;++r) std::memcpy(dstUv+r*uvRow,uv+size_t(r)*pitch,uvRow);
  unlock();
  timings_.copyMs+=ms_since(tCopy); ++timings_.frames;
  f->ptsUs=pts; f->width=w; f->height=h; f->rotation=info_.rotation;
  f->cropLeft=info_.cropLeft; f->cropTop=info_.cropTop; f->visibleWidth=info_.visible_width(); f->visibleHeight=info_.visible_height();
  f->format=format_; f->color=info_.color;
  f->planes[0]=dst; f->planes[1]=dstUv; f->strides[0]=static_cast<u32>(yRow); f->strides[1]=static_cast<u32>(uvRow); f->planeCount=2;
  out=FrameRef::adopt(f); return OkStatus;
 }
 Status rgb_frame(IMFSample* sample,i64 pts,FrameRef& out) noexcept {
  ComPtr<IMFMediaBuffer> buffer; if(FAILED(sample->ConvertToContiguousBuffer(&buffer))) return failure(Errc::DecodeFailed,"Windows: buffer de video");
  BYTE* top=nullptr; LONG pitch=LONG(info_.codedWidth*4); DWORD len=0;
  ComPtr<IMF2DBuffer> two; buffer.As(&two);
  const bool locked2d=two && SUCCEEDED(two->Lock2D(&top,&pitch));
  BYTE* raw=nullptr;
  if(!locked2d) {
   if(FAILED(buffer->Lock(&raw,nullptr,&len))) return failure(Errc::DecodeFailed,"Windows: leitura do quadro");
   pitch=defaultStride_?defaultStride_:LONG(info_.codedWidth*4);
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

// -----------------------------------------------------------------------------
// Saída de áudio. WASAPI compartilhado por evento, numa thread MMCSS "Pro
// Audio", com ~40 ms de buffer: o waveOut antigo (3 × 10 ms) estalava a cada
// soluço do agendador. waveOut fica só de reserva, se o WASAPI não abrir.
// -----------------------------------------------------------------------------
class Sound final : public audio::AudioOutput {
 static constexpr int kBuffers=6;   // 60 ms: reserva, mas sem os estalos de 30 ms
 HWAVEOUT device_=nullptr;HANDLE event_=nullptr;audio::AudioRenderFn fn_=nullptr;void* ctx_=nullptr;
 std::atomic<bool> running_{false};std::thread worker_;std::mutex mutex_;i64 base_=0;
public:
 ~Sound() override {close();}
 Status open(audio::AudioRenderFn fn,void* ctx) noexcept override {
  close();if(!fn)return failure(Errc::InvalidArgument,"Windows: callback de audio ausente");
  fn_=fn;ctx_=ctx;base_=0;event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  if(!event_)return failure(Errc::NotSupported,"Windows: evento de audio indisponivel");
  WAVEFORMATEX fmt{WAVE_FORMAT_IEEE_FLOAT,2,48000,48000*8,8,32,0};
  if(waveOutOpen(&device_,WAVE_MAPPER,&fmt,reinterpret_cast<DWORD_PTR>(event_),0,CALLBACK_EVENT)!=MMSYSERR_NOERROR){close();return failure(Errc::NotSupported,"Windows: saida de audio indisponivel");}return OkStatus;
 }
 Status start() noexcept override {
  if(running_.exchange(true))return OkStatus;
  if(!device_){running_=false;return failure(Errc::InvalidState,"Windows: audio fechado");}
  worker_=std::thread([this]{
   std::array<std::array<float,960>,kBuffers> pcm{};std::array<WAVEHDR,kBuffers> heads{};
   for(size_t i=0;i<heads.size();++i){heads[i].lpData=reinterpret_cast<LPSTR>(pcm[i].data());heads[i].dwBufferLength=sizeof(pcm[i]);waveOutPrepareHeader(device_,&heads[i],sizeof(WAVEHDR));}
   while(running_){for(auto& h:heads)if(!(h.dwFlags&WHDR_INQUEUE)){fn_(ctx_,reinterpret_cast<float*>(h.lpData),480);waveOutWrite(device_,&h,sizeof(h));}WaitForSingleObject(event_,10);}
   {std::lock_guard<std::mutex> guard(mutex_);MMTIME time{};time.wType=TIME_SAMPLES;if(waveOutGetPosition(device_,&time,sizeof(time))==0&&time.wType==TIME_SAMPLES)base_+=time.u.sample;waveOutReset(device_);}
   for(auto& h:heads)waveOutUnprepareHeader(device_,&h,sizeof(h));
  });return OkStatus;
 }
 void stop() noexcept override {running_=false;if(event_)SetEvent(event_);if(worker_.joinable())worker_.join();}
 void close() noexcept override {stop();if(device_){waveOutClose(device_);device_=nullptr;}if(event_){CloseHandle(event_);event_=nullptr;}}
 bool presented(u64,i64& frames) noexcept override {std::lock_guard<std::mutex> guard(mutex_);if(!device_)return false;MMTIME t{};t.wType=TIME_SAMPLES;if(waveOutGetPosition(device_,&t,sizeof(t))!=0||t.wType!=TIME_SAMPLES)return false;frames=base_+t.u.sample;return true;}
 u32 latency_frames() const noexcept override {return kBuffers*480;}
};

// Troca do dispositivo padrão (fone ligado, saída escolhida no Windows): só
// avisa a thread de áudio, que reabre no próximo giro. Nada de bloquear aqui.
class DeviceWatch final : public IMMNotificationClient {
 std::atomic<ULONG> refs_{1}; std::atomic<bool>* changed_; HANDLE wake_;
public:
 DeviceWatch(std::atomic<bool>* changed,HANDLE wake):changed_(changed),wake_(wake){}
 ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
 ULONG STDMETHODCALLTYPE Release() override {const ULONG n=--refs_;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
  if(id==__uuidof(IUnknown)||id==__uuidof(IMMNotificationClient)){*out=static_cast<IMMNotificationClient*>(this);AddRef();return S_OK;}
  *out=nullptr;return E_NOINTERFACE;
 }
 HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole role,LPCWSTR) override {
  if(flow==eRender&&role==eConsole){changed_->store(true);SetEvent(wake_);}return S_OK;
 }
 HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD) override {return S_OK;}
 HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {return S_OK;}
 HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {return S_OK;}
 HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY) override {return S_OK;}
};

class Wasapi final : public audio::AudioOutput {
 static constexpr REFERENCE_TIME kBufferHns=400'000;   // 40 ms
 static constexpr u32 kChunk=480;                      // o mixer recebe até 10 ms por chamada
 audio::AudioRenderFn fn_=nullptr; void* ctx_=nullptr;
 std::thread worker_; HANDLE wake_=nullptr, event_=nullptr;
 std::atomic<bool> quit_{false}, want_{false}, changed_{false};
 std::mutex stateMutex_; std::condition_variable stateCv_; int ready_=-1; bool applied_=false;
 // Só a thread de áudio toca nestes.
 ComPtr<IMMDeviceEnumerator> enumerator_; ComPtr<IAudioClient> client_; ComPtr<IAudioRenderClient> render_; ComPtr<IAudioClock> clock_;
 UINT64 clockFreq_=0; u32 bufferFrames_=0; bool playing_=false; bool fresh_=false;
 // Posição publicada para `presented` (seqlock): quadros já tocados (na escala
 // dos entregues ao callback) e o QPC em que valiam.
 std::atomic<u32> seq_{0}; std::atomic<i64> posFrames_{0}, posQpc_{0}; std::atomic<bool> posRunning_{false};
 std::atomic<i64> delivered_{0}; i64 streamBase_=0; std::atomic<i64> lastPresented_{0};
 std::atomic<u32> latency_{0}, streamLatency_{0}, bufferReport_{0};
 std::atomic<u64> glitches_{0}, reopens_{0};
 static i64 qpc100() { LARGE_INTEGER c{},f{}; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return static_cast<i64>(static_cast<double>(c.QuadPart)*1e7/static_cast<double>(f.QuadPart)); }
 void publish(i64 frames,i64 qpc,bool running) noexcept {
  seq_.fetch_add(1,std::memory_order_acq_rel);
  posFrames_.store(frames,std::memory_order_relaxed); posQpc_.store(qpc,std::memory_order_relaxed); posRunning_.store(running,std::memory_order_relaxed);
  seq_.fetch_add(1,std::memory_order_acq_rel);
 }
 // Posição do dispositivo → quadros entregues.
 void sample_clock(bool running) noexcept {
  if(!clock_||!clockFreq_) return;
  UINT64 pos=0,qpc=0; if(FAILED(clock_->GetPosition(&pos,&qpc))) return;
  const i64 frames=streamBase_+static_cast<i64>(static_cast<double>(pos)*audio::kMixRate/static_cast<double>(clockFreq_));
  publish(frames,static_cast<i64>(qpc),running);
 }
 void release_stream() noexcept {
  if(client_&&playing_) client_->Stop();
  render_.Reset(); clock_.Reset(); client_.Reset(); playing_=false;
  // O que estava no buffer não toca mais: a posição continua de onde chegou.
  streamBase_=delivered_.load(); publish(streamBase_,qpc100(),false);
 }
 HRESULT open_stream() noexcept {
  release_stream();
  ComPtr<IMMDevice> device; HRESULT hr=enumerator_->GetDefaultAudioEndpoint(eRender,eConsole,&device); if(FAILED(hr)) return hr;
  if(FAILED(hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client_.GetAddressOf())))) return hr;
  // 48 kHz estéreo float sempre; o conversor do sistema adapta ao formato do
  // dispositivo (44,1 kHz, 24 bits, 5.1...) com o resampler de qualidade.
  WAVEFORMATEX fmt{WAVE_FORMAT_IEEE_FLOAT,static_cast<WORD>(audio::kMixChannels),audio::kMixRate,audio::kMixRate*audio::kMixChannels*4,static_cast<WORD>(audio::kMixChannels*4),32,0};
  const DWORD flags=AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY|AUDCLNT_STREAMFLAGS_NOPERSIST;
  if(FAILED(hr=client_->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,kBufferHns,0,&fmt,nullptr))){client_.Reset();return hr;}
  UINT32 frames=0; REFERENCE_TIME lat=0;
  if(FAILED(hr=client_->SetEventHandle(event_))||FAILED(hr=client_->GetBufferSize(&frames))
   ||FAILED(hr=client_->GetService(IID_PPV_ARGS(&render_)))||FAILED(hr=client_->GetService(IID_PPV_ARGS(&clock_)))
   ||FAILED(hr=clock_->GetFrequency(&clockFreq_))){release_stream();return hr;}
  client_->GetStreamLatency(&lat);
  bufferFrames_=frames;
  const u32 streamFrames=static_cast<u32>(lat*audio::kMixRate/10'000'000);
  streamLatency_.store(streamFrames); bufferReport_.store(frames); latency_.store(streamFrames+frames);
  streamBase_=delivered_.load(); publish(streamBase_,qpc100(),false);
  AUREA_LOG_INFO("audio: WASAPI %u Hz, buffer %u quadros (%.1f ms), latencia do stream %.1f ms",audio::kMixRate,frames,frames*1000.0/audio::kMixRate,lat/10'000.0);
  return S_OK;
 }
 HRESULT fill() noexcept {
  UINT32 pad=0; HRESULT hr=client_->GetCurrentPadding(&pad); if(FAILED(hr)) return hr;
  // Fila vazia tocando = buraco audível.
  if(pad==0&&!fresh_) glitches_.fetch_add(1,std::memory_order_relaxed);
  fresh_=false;
  const UINT32 n=bufferFrames_>pad?bufferFrames_-pad:0;
  if(n) {
   BYTE* data=nullptr; if(FAILED(hr=render_->GetBuffer(n,&data))) return hr;
   f32* out=reinterpret_cast<f32*>(data);
   for(u32 done=0;done<n;){const u32 c=std::min(kChunk,n-done);fn_(ctx_,out+static_cast<size_t>(done)*audio::kMixChannels,c);done+=c;}
   if(FAILED(hr=render_->ReleaseBuffer(n,0))) return hr;
   delivered_.fetch_add(n,std::memory_order_release);
  }
  return S_OK;
 }
 HRESULT begin() noexcept {
  // Buffer cheio antes do Start: o começo não sai picotado.
  fresh_=true; HRESULT hr=fill(); fresh_=true; if(FAILED(hr)) return hr;
  if(FAILED(hr=client_->Start())) return hr;
  playing_=true; return S_OK;
 }
 void run() noexcept {
  apartment();
  DWORD task=0; HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
  if(!mmcss) SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_TIME_CRITICAL);
  ComPtr<DeviceWatch> watch;
  HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator_));
  if(SUCCEEDED(hr)) hr=open_stream();
  if(SUCCEEDED(hr)){watch.Attach(new DeviceWatch(&changed_,wake_));enumerator_->RegisterEndpointNotificationCallback(watch.Get());}
  { std::lock_guard<std::mutex> g(stateMutex_); ready_=SUCCEEDED(hr)?1:0; }
  stateCv_.notify_all();
  if(FAILED(hr)){AUREA_LOG_WARN("audio: WASAPI nao abriu (0x%08lx)",hr);enumerator_.Reset();if(mmcss)AvRevertMmThreadCharacteristics(mmcss);return;}
  bool broken=false; u64 retryAt=0;
  while(!quit_.load()) {
   const HANDLE hs[2]={wake_,event_};
   const DWORD timeout=playing_?200:(broken||!client_?250:INFINITE);
   const DWORD w=WaitForMultipleObjects(2,hs,FALSE,timeout);
   if(quit_.load()) break;
   if(playing_&&w==WAIT_TIMEOUT) broken=true;   // tocando sem evento: dispositivo sumiu
   if(changed_.exchange(false)) broken=true;
   if(broken&&GetTickCount64()>=retryAt) {
    // Fone tirado ou padrão trocado: reabre no novo padrão, do ponto em que estava.
    const bool resume=want_.load();
    hr=open_stream();
    if(SUCCEEDED(hr)&&resume) hr=begin();
    if(SUCCEEDED(hr)){broken=false;reopens_.fetch_add(1);AUREA_LOG_INFO("audio: saida reaberta no dispositivo padrao");}
    else {release_stream();retryAt=GetTickCount64()+500;}
   }
   const bool want=want_.load();
   if(client_&&!broken) {
    if(want&&!playing_){hr=begin();if(FAILED(hr))broken=true;}
    else if(!want&&playing_){
     client_->Stop(); sample_clock(false); client_->Reset(); playing_=false;
     streamBase_=delivered_.load(); publish(streamBase_,qpc100(),false);
    }
   }
   { std::lock_guard<std::mutex> g(stateMutex_); applied_=want; }
   stateCv_.notify_all();
   if(playing_&&!broken) {
    hr=fill();
    if(FAILED(hr)){AUREA_LOG_WARN("audio: WASAPI falhou (0x%08lx); reabrindo",hr);broken=true;release_stream();}
    else sample_clock(true);
   }
  }
  if(watch&&enumerator_) enumerator_->UnregisterEndpointNotificationCallback(watch.Get());
  release_stream(); enumerator_.Reset();
  if(mmcss) AvRevertMmThreadCharacteristics(mmcss);
 }
 bool wait_applied(bool want) noexcept {
  std::unique_lock<std::mutex> g(stateMutex_);
  return stateCv_.wait_for(g,std::chrono::seconds(1),[&]{return applied_==want;});
 }
public:
 ~Wasapi() override {close();}
 Status open(audio::AudioRenderFn fn,void* ctx) noexcept override {
  close(); if(!fn) return failure(Errc::InvalidArgument,"Windows: callback de audio ausente");
  fn_=fn; ctx_=ctx; quit_=false; want_=false; changed_=false; ready_=-1; applied_=false;
  wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr); event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  if(!wake_||!event_){close();return failure(Errc::NotSupported,"Windows: evento de audio indisponivel");}
  worker_=std::thread([this]{run();});
  bool ok=false;
  { std::unique_lock<std::mutex> g(stateMutex_); stateCv_.wait_for(g,std::chrono::seconds(3),[&]{return ready_!=-1;}); ok=ready_==1; }
  if(!ok){close();return failure(Errc::NotSupported,"Windows: WASAPI indisponivel");}
  return OkStatus;
 }
 Status start() noexcept override {
  if(!worker_.joinable()) return failure(Errc::InvalidState,"Windows: audio fechado");
  want_=true; SetEvent(wake_); (void)wait_applied(true); return OkStatus;
 }
 void stop() noexcept override {
  if(!worker_.joinable()) return;
  want_=false; SetEvent(wake_); (void)wait_applied(false);
 }
 void close() noexcept override {
  if(worker_.joinable()){quit_=true;SetEvent(wake_);worker_.join();}
  if(wake_){CloseHandle(wake_);wake_=nullptr;} if(event_){CloseHandle(event_);event_=nullptr;}
 }
 bool presented(u64 nowNs,i64& frames) noexcept override {
  i64 f=0,q=0; bool running=false;
  for(int tries=0;;++tries) {
   const u32 s0=seq_.load(std::memory_order_acquire);
   if(!(s0&1u)) {
    f=posFrames_.load(std::memory_order_relaxed); q=posQpc_.load(std::memory_order_relaxed); running=posRunning_.load(std::memory_order_relaxed);
    if(seq_.load(std::memory_order_acquire)==s0) break;
   }
   if(tries>64) return false;
  }
  if(running) {
   // Entre as leituras do relógio (a cada período) a posição anda no tempo:
   // `nowNs` no relógio do QPC, sem extrapolar mais que 30 ms (thread parada).
   const i64 elapsed=qpc100()-q+(static_cast<i64>(nowNs)-static_cast<i64>(monotonic_ns()))/100;
   f+=std::clamp<i64>(elapsed,0,300'000)*audio::kMixRate/10'000'000;
  }
  f=std::min(f,delivered_.load(std::memory_order_acquire));
  // Nunca volta (reabertura, leitura de outra thread no meio).
  i64 last=lastPresented_.load(std::memory_order_relaxed);
  while(f>last&&!lastPresented_.compare_exchange_weak(last,f)){}
  frames=std::max(f,last);
  return true;
 }
 u32 latency_frames() const noexcept override {return latency_.load(std::memory_order_relaxed);}
 void report(AudioOutputReport& r) const noexcept {
  r.backend="WASAPI"; r.bufferFrames=bufferReport_.load(); r.streamLatencyFrames=streamLatency_.load();
  r.glitches=glitches_.load(); r.reopens=reopens_.load();
 }
};

// WASAPI; se não abrir (serviço de áudio parado, sem dispositivo), waveOut.
// O mesmo Wasapi entre aberturas: a contagem de quadros entregues (a escala
// de `presented`) continua, como o `written_` do mixer.
class PcAudio final : public audio::AudioOutput {
 Wasapi wasapi_; Sound wave_; audio::AudioOutput* impl_=nullptr;
 // AUREA_AUDIO_SILENT=1 (testes): o mixer roda e o relógio do dispositivo
 // anda, mas o que sai são zeros — nada toca nas caixas.
 audio::AudioRenderFn userFn_=nullptr; void* userCtx_=nullptr;
 static void silent(void* self,f32* out,u32 frames) {
  auto* p=static_cast<PcAudio*>(self);
  if(p->userFn_) p->userFn_(p->userCtx_,out,frames);
  std::memset(out,0,sizeof(f32)*audio::kMixChannels*frames);
 }
public:
 ~PcAudio() override {close();}
 Status open(audio::AudioRenderFn fn,void* ctx) noexcept override {
  close();
  char quiet[4]{};
  if(GetEnvironmentVariableA("AUREA_AUDIO_SILENT",quiet,sizeof(quiet))&&quiet[0]=='1'){userFn_=fn;userCtx_=ctx;fn=&silent;ctx=this;}
  if(wasapi_.open(fn,ctx).ok()){impl_=&wasapi_;return OkStatus;}
  AUREA_LOG_WARN("audio: WASAPI indisponivel; usando waveOut");
  const Status st=wave_.open(fn,ctx); impl_=st.ok()?&wave_:nullptr; return st;
 }
 Status start() noexcept override {return impl_?impl_->start():failure(Errc::InvalidState,"Windows: audio fechado");}
 void stop() noexcept override {if(impl_)impl_->stop();}
 void close() noexcept override {if(impl_)impl_->close();impl_=nullptr;}
 bool presented(u64 now,i64& frames) noexcept override {return impl_&&impl_->presented(now,frames);}
 u32 latency_frames() const noexcept override {return impl_?impl_->latency_frames():0;}
 bool report(AudioOutputReport& r) const noexcept {
  if(!impl_) return false;
  if(impl_==&wasapi_) wasapi_.report(r);
  else {r=AudioOutputReport{};r.backend="waveOut";r.bufferFrames=impl_->latency_frames();}
  return true;
 }
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
 if(!rgba||!w||!h||u64(w)*h>67108864)return false;
 apartment();ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> enc;ComPtr<IWICBitmapFrameEncode> frame;
 if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&enc))||FAILED(enc->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(enc->CreateNewFrame(&frame,nullptr))||FAILED(frame->Initialize(nullptr)))return false;
 if(FAILED(frame->SetSize(w,h)))return false;
 // The native PNG encoder accepts BGRA, whereas requesting RGBA may silently
 // negotiate another format. Supply the negotiated bytes explicitly so red,
 // blue and alpha survive both exported frames and imported image round trips.
 WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
 if(FAILED(frame->SetPixelFormat(&format))||format!=GUID_WICPixelFormat32bppBGRA)return false;
 std::vector<u8> bgra(size_t(w)*h*4);
 for(size_t i=0;i<bgra.size();i+=4){bgra[i]=rgba[i+2];bgra[i+1]=rgba[i+1];bgra[i+2]=rgba[i];bgra[i+3]=rgba[i+3];}
 return SUCCEEDED(frame->WritePixels(h,w*4,w*h*4,bgra.data()))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(enc->Commit());
}
bool MediaFactory::probe(const char* path,MediaProbe& out){out={};Video v;Audio a;if(v.open(path,true)){out.hasVideo=true;out.video=v.info();}if(a.open(path)){out.hasAudio=true;out.audioSampleRate=a.info().sampleRate;out.audioChannels=a.info().channels;out.audioDurationUs=a.info().durationUs;}return out.hasVideo||out.hasAudio;}
std::unique_ptr<VideoDecoderBackend> MediaFactory::open_video(const Asset& asset,MediaPriority priority){
 // Só a tentativa compatível do export (o decoder do aparelho falhou antes) vai sem DXVA.
 auto v=std::make_unique<Video>();return v->open(asset.sourcePath.c_str(),priority!=MediaPriority::ExportSoftwareDecode)?std::move(v):nullptr;
}
std::unique_ptr<audio::AudioDecoderBackend> MediaFactory::open_audio(const char* path){auto a=std::make_unique<Audio>();return a->open(path)?std::move(a):nullptr;}
std::unique_ptr<audio::AudioOutput> make_audio_output(){return std::make_unique<PcAudio>();}
bool video_decode_timings(const VideoDecoderBackend* video,VideoDecodeTimings& out){
 auto* v=dynamic_cast<const Video*>(video); if(!v) return false; out=v->timings_; return true;
}
bool audio_output_report(audio::AudioOutput* out,AudioOutputReport& r){
 auto* pc=dynamic_cast<PcAudio*>(out); return pc&&pc->report(r);
}
}
