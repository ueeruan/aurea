#include "WindowsMedia.hpp"
#include "TimelineGeometry.hpp"
#include "VulkanBackend.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <cmath>

using namespace aurea;
using namespace aurea::windows;
int run_desktop_tests(const std::wstring& directory) {
 namespace fs=std::filesystem;fs::create_directories(directory);std::ofstream log(fs::path(directory)/L"desktop-tests.txt");int failed=0,total=0;
 auto check=[&](bool ok,const char* name){++total;log<<(ok?"PASS ":"FAIL ")<<name<<std::endl;if(!ok)++failed;};
 for(double zoom:{.1,1.,3.7,17.,50.})for(double scroll:{0.,53.25,1234.5}){
  TimelineGeometry geometry{312,scroll,zoom};for(i64 time:{0,1,17,300,4000}){check(geometry.frame(geometry.x(double(time)))==time,"frame/pixel round trip");check(geometry.key_time(geometry.key_x(time,93,21),93,21)==time,"keyframe local/timeline round trip");}
  const double x=geometry.x(123.);geometry.zoom_at(x,zoom*1.7);check(std::abs(geometry.x(123.)-x)<1e-7,"zoom preserves cursor anchor");
 }
 MediaFactory factory;Engine engine;EngineConfig cfg;cfg.backend=new vk::Backend;cfg.mediaFactory=&factory;cfg.imageLoader=load_image;cfg.exportSinkFactory=make_export_sink;cfg.disableAutosave=true;cfg.workerCount=2;cfg.documentsDirectory=utf8(directory);cfg.cacheDirectory=utf8((fs::path(directory)/L"cache").wstring());cfg.defaultFontPath="C:/Windows/Fonts/segoeui.ttf";
 auto status=engine.initialize(cfg);check(status.ok(),"engine initializes with Windows Vulkan and media adapters");if(!status.ok()){log<<status.message()<<std::endl;return 1;}
 check(engine.new_project(320,180,30,"Desktop test").ok(),"create composition");
 const auto shape=engine.add_shape(0);check(shape.ok(),"add shape");
 if(shape.ok()){
  Command c;c.type=CommandType::CompositionSetDuration;u64 comp;u32 w,h;f64 fps;i64 duration;f32 bg[4];engine.query_composition(comp,w,h,fps,duration,bg);c.comp_duration={CompositionId::unpack(comp),FrameIndex{30}};check(engine.apply_command(c).ok(),"set duration");
  c={};c.type=CommandType::KeyframeInsert;c.keyframe={TrackRef{LayerId::unpack(*shape),TrackProperty::PositionX,kInvalidIndex,0},FrameIndex{0},80};check(engine.apply_command(c).ok(),"insert first key");c.keyframe.time=FrameIndex{20};c.keyframe.value=240;check(engine.apply_command(c).ok(),"insert second key");
  c={};c.type=CommandType::LayerDuplicate;c.layer_ref.layer=LayerId::unpack(*shape);check(engine.apply_command(c).ok(),"duplicate");u64 copy=0;engine.get_selection(&copy,1);check(copy!=*shape,"duplicate has independent identity");
  c={};c.type=CommandType::Undo;check(engine.apply_command(c).ok(),"undo duplicate");
 }
 const auto png=fs::path(directory)/L"source.png";std::vector<u8> pixels(32*24*4);for(size_t i=0;i<pixels.size();i+=4){pixels[i]=220;pixels[i+1]=40;pixels[i+2]=80;pixels[i+3]=255;}check(save_png(png.wstring(),pixels.data(),32,24),"WIC writes PNG");ImagePixels decoded;check(load_image(utf8(png.wstring()).c_str(),decoded,nullptr)&&decoded.rgba==pixels,"WIC PNG round trip");
 auto image=engine.import_image(decoded.rgba.data(),decoded.width,decoded.height,"Imagem",utf8(png.wstring()).c_str());check(image.ok(),"import image");
 check(engine.add_text("AUREA Desktop").ok(),"text engine available");
 const auto project=fs::path(directory)/L"test.aurea";check(engine.save_project(utf8(project.wstring()).c_str()).ok(),"save shared aurea format");const auto before=engine.read_status().layerCount;
 check(engine.load_project(utf8(project.wstring()).c_str()).ok()&&engine.read_status().layerCount==before,"reopen without losing layers");
 std::vector<u8> frame;u32 w=0,h=0;auto capture=engine.capture_frame_rgba(320,frame,w,h);check(capture.ok()&&w==320&&h==180,"render shared engine with Vulkan");if(capture.ok())check(save_png((fs::path(directory)/L"preview.png").wstring(),frame.data(),w,h),"save rendered preview");
 // Real Windows encode -> probe -> video decode, including audio muxing.
 auto sink=make_export_sink(nullptr);VideoStreamConfig v;v.width=64;v.height=48;v.fps=30;v.bitrateBps=1000000;AudioStreamConfig a;const auto videoPath=utf8((fs::path(directory)/L"media.mp4").wstring());
 auto opened=sink->open(videoPath.c_str(),v,&a);check(opened.ok(),"open H264/AAC sink");
 if(opened.ok()){std::vector<u8> y(64*48,90),uv(64*24,128);std::vector<i16> pcm(1600*2);for(size_t i=0;i<pcm.size()/2;++i)pcm[i*2]=pcm[i*2+1]=i16(std::sin(double(i)*440*6.2831853/48000)*12000);
  bool encoded=true;for(int i=0;i<30;++i){encoded&=sink->write_video(y.data(),64,uv.data(),64,i64(i)*1000000/30).ok();encoded&=sink->write_audio(pcm.data(),1600,i64(i)*1000000/30).ok();}check(encoded&&sink->finish().ok(),"encode real 1s MP4 with audio");
  MediaProbe probe;check(factory.probe(videoPath.c_str(),probe)&&probe.hasVideo&&probe.hasAudio&&probe.video.display_width()==64,"probe Windows media");Asset asset;asset.sourcePath=videoPath;auto source=factory.open_video(asset,MediaPriority::Export);check(bool(source),"open Windows video decoder");if(source){FrameRef f;bool eos=false;i64 pts=0;for(int i=0;i<30&&!f&&!eos;++i)source->next_frame(0,f,pts,eos);check(bool(f)&&f->width==64&&f->planes[0][0]>50,"decode real video pixels");check(source->seek_to_keyframe(500000).ok(),"seek decoder");}
  auto audio=factory.open_audio(videoPath.c_str());check(bool(audio),"open audio decoder");if(audio){std::vector<float> samples;i64 pts=0;bool eos=false;for(int i=0;i<30&&samples.empty()&&!eos;++i)audio->read(samples,pts,eos);check(!samples.empty(),"decode AAC samples");}
  VideoImport request;request.sourcePath=videoPath;request.displayName="Video";check(engine.import_video(request).ok(),"import video through Engine");
 }
 ExportSettings settings;settings.width=320;settings.height=180;settings.fps=30;const auto exportPath=utf8((fs::path(directory)/L"composition.mp4").wstring());const auto started=engine.start_export(settings,exportPath.c_str());check(started.ok(),"start composition MP4 export");if(started.ok()){for(int i=0;i<600&&engine.export_progress().running;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));const auto progress=engine.export_progress();check(progress.finished&&progress.result==Errc::Ok&&progress.framesDone==progress.framesTotal,"complete real composition export");if(progress.running)(void)engine.cancel_export();}
 check(engine.add_shape3d(0,"Cubo").ok(),"add 3D shape");check(engine.capture_frame_rgba(320,frame,w,h).ok(),"render 3D on Windows");
 engine.shutdown();log<<"TOTAL "<<total<<" FAILED "<<failed<<std::endl;return failed?1:0;
}
