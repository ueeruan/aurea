// Rebuilds the supplied 17-second motion as editable Aurea layers. No reference
// video frames, flattened UI screenshots or TikTok marks are used in the project.
#include "VulkanBackend.hpp"
#include "aurea/Engine.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/vector/Vector.hpp"
#include "ImageIO.hpp"
#include <filesystem>
#include <cstdio>
#include <cmath>
#include <thread>
using namespace aurea;
static Engine engine;
static Composition* comp;
static const std::string root = "C:/Users/Ruan/Documents/Aureabeta/output/hera-motion-aurea/";
static LayerId parent;
static const Vec4 white{1,1,1,1}, ink{.028f,.028f,.03f,1}, coral{1,.278f,.224f,1};
static void check(Status s) { if (!s) { fprintf(stderr,"%.*s\n",int(s.detail().size()),s.detail().data()); std::exit(2); } }
static Layer& layer(LayerId id) { return *comp->layer(id); }
static void key(LayerId id, TrackProperty prop, int frame, float value, Interpolation ease=Interpolation::EaseInOut) {
    (void)layer(id).tracks.get_or_create(prop).set(FrameIndex{frame},value,ease);
}
static LayerId create(LayerKind kind,const std::string& name,int start,int end) {
    auto id=comp->add_layer(kind,name); auto& l=layer(id); l.start=FrameIndex{start};l.end=FrameIndex{end};l.parent=parent;l.motionBlur=true;return id;
}
static LayerId rect(const std::string& name,int a,int b,float x,float y,float w,float h,Vec4 color,float radius=0,bool ellipse=false) {
    auto id=create(LayerKind::Shape,name,a,b);auto& l=layer(id);l.shape.shapeType=ellipse?1:0;l.shape.bounds={0,0,w,h};l.shape.fillColor=color;l.shape.cornerRadius=radius;l.transform.position={x,y,0};l.transform.anchor={w/2,h/2,0};return id;
}
static LayerId label(const std::string& value,int a,int b,float x,float y,float size,Vec4 color=ink,bool bold=false) {
    auto id=create(LayerKind::Text,value,a,b);auto& l=layer(id);l.text.content=value;l.text.fontPath=root+"assets/Roboto-Regular.ttf";l.text.size=size;l.text.fontWeight=bold?800:400;l.text.color=color;
    auto f=text::default_font();const auto ext=text::measure(*f,l.text);l.transform.anchor={std::ceil(ext.width+4)/2,std::ceil(ext.height+4)/2,0};l.transform.position={x,y,0};return id;
}
static void entrance(LayerId id,int frames=12,float distance=18) {
    const auto y=layer(id).transform.position.y;key(id,TrackProperty::Opacity,0,0);key(id,TrackProperty::Opacity,frames,1);
    key(id,TrackProperty::PositionY,0,y+distance);key(id,TrackProperty::PositionY,frames,y);
}
static void scale(LayerId id,int t,float s) { key(id,TrackProperty::ScaleX,t,s);key(id,TrackProperty::ScaleY,t,s); }
static LayerId group(const std::string& name,int a,int b) {
    parent={};auto id=create(LayerKind::Null,name,a,b);layer(id).transform.anchor={512,288,0};layer(id).transform.position={512,288,0};parent=id;return id;
}
static LayerId svg(const std::string& name,int a,int b,const std::string& body,float x=0,float y=0) {
    vector::SvgResult parsed;std::string error;
    if(!vector::parse_svg("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1024\" height=\"576\" viewBox=\"0 0 1024 576\">"+body+"</svg>",parsed,&error)) { fprintf(stderr,"SVG: %s\n",error.c_str()); std::exit(3); }
    auto id=create(LayerKind::Shape,name,a,b);auto& l=layer(id);l.shape.shapeType=kShapeVector;l.shape.vector=std::move(parsed.data);l.transform.position={x,y,0};return id;
}
static void background(int a,int b,Vec4 color) { auto save=parent;parent={};rect("Fundo",a,b,512,288,1024,576,color);parent=save; }
static void warmLight(int a,int b,bool dark) {
    auto save=parent;parent={};
    const std::string defs="<defs><radialGradient id=\"g\"><stop offset=\"0\" stop-color=\"#ffcf8e\" stop-opacity=\".95\"/><stop offset=\".32\" stop-color=\"#ff843d\" stop-opacity=\".72\"/><stop offset=\".64\" stop-color=\"#ce3817\" stop-opacity=\".35\"/><stop offset=\"1\" stop-color=\"#ce3817\" stop-opacity=\"0\"/></radialGradient></defs>";
    auto glow=svg("Luz radial quente • vetorial",a,b,defs+"<ellipse cx=\"512\" cy=\"650\" rx=\"940\" ry=\"520\" fill=\"url(#g)\"/>");
    key(glow,TrackProperty::PositionY,0,dark?210:0);key(glow,TrackProperty::PositionY,b-a-1,dark?-180:80);
    parent=save;
}
static void cursor(int a,int b,float x,float y) { svg("Cursor editável",a,b,"<path d=\"M0 0L0 28L7 21L13 33L18 30L12 19L23 19Z\" fill=\"#111\" stroke=\"#fff\" stroke-width=\"2\"/>",x,y); }
static void prompt(int a,int b,bool change) {
    auto g=group(change?"05 • Prompt de alteração":"02 • Prompt de criação",a,b);
    background(a,b,white);
    auto card=rect("Cartão de prompt",a,b,512,290,650,244,{.985f,.985f,.986f,1},42);
    layer(card).shape.strokeWidth=change?1.5f:4;layer(card).shape.strokeColor=change?Vec4{.84f,.84f,.85f,1}:Vec4{.49f,.82f,.9f,1};
    const std::string phrase=change?"Change it to Hera":"Create me a launch video";
    // Separate glyph layers implement an editable typewriter without baked pixels.
    float pen=change?231:240;
    for(size_t i=0;i<phrase.size();++i) {
        std::string ch(1,phrase[i]); TextData metrics;metrics.content=ch;metrics.size=28;auto ex=text::measure(*text::default_font(),metrics);
        const int begin=a+5+static_cast<int>(i)*1;
        label(ch,begin,b,pen+ex.width/2,change?273:242,28);pen+=ex.width;
    }
    auto caret=rect("Cursor de texto",a+5,b,240,change?273:242,2,31,ink);
    key(caret,TrackProperty::PositionX,0,240,Interpolation::Linear);key(caret,TrackProperty::PositionX,static_cast<int>(phrase.size()),pen+2,Interpolation::Linear);
    rect("Enviar",a,b,780,364,40,40,coral,20,true);label("↑",a,b,780,364,28,white);
    if(change) {
        label("≡  ▧  ▷  +",a,b,315,204,23,{.48f,.48f,.49f,1});label("Select a style",a,b,719,204,19,{.79f,.4f,.42f,1});
        label("+",a,b,248,364,32,{.45f,.45f,.47f,1});label("Chat",a,b,717,364,16,{.4f,.4f,.4f,1});
    } else { rect("Anexar",a,b,726,364,40,40,ink,20,true);label("+",a,b,726,364,27,white); }
    scale(g,0,.87f);scale(g,14,1);scale(g,b-a-8,1);scale(g,b-a-1,1.12f);
}
static void editor(int a,int b,bool hera) {
    auto g=group(hera?"06 • Editor / Hera":"04 • Editor / karlozyx",a,b);background(a,b,{.974f,.972f,.969f,1});
    rect("Painel lateral",a,b,106,288,200,564,{.993f,.993f,.993f,1},10);
    label("Product Showcase",a,b,96,20,12,ink,true);label("Chat                 History",a,b,104,54,10);
    const char* lines[]={"I'd like to clarify a few things","before creating your animation:","","1. What's the main message?","Product showcase, brand story,","call-to-action, promotional offer?","","2. What's the tone and style?","Energetic, clean & modern,","casual & relatable, bold & punchy?","","3. Any specific visuals?","Product shots, logo, text, colors.","","This will help me create an ad","that fits your story perfectly."};
    for(int i=0;i<16;i++) label(lines[i],a,b,105,104+12.f*i,8,{.22f,.22f,.23f,1});
    rect("Opções",a,b,105,359,175,89,{.952f,.952f,.954f,1},5);label("Options",a,b,51,327,9);label("Animation Style",a,b,71,349,8);label("• Smooth and fluid",a,b,87,366,8);label("• Social media ad",a,b,85,386,8);
    rect("Toolbar",a,b,610,54,488,35,white,10);label("↗   ▤   ●   ◇   Widescreen (16:9)   ◷ Auto (max 60s)   ↶",a,b,610,54,13);
    auto canvas=rect("Canvas editável",a,b,617,300,768,431,{.995f,.993f,.992f,1},24);layer(canvas).shape.strokeWidth=1;layer(canvas).shape.strokeColor={1,.92f,.92f,1};
    const auto name=hera?"Hera":"karlozyx.";auto title=label(name,a,b,617,300,hera?55:57,hera?coral:ink,true);
    auto bounds=rect("Seleção do título",a,b,617,300,hera?155:255,75,{0,0,0,0});layer(bounds).shape.strokeWidth=1;layer(bounds).shape.strokeColor={.79f,.38f,.29f,1};
    for(float x:{hera?539.f:489.f,hera?695.f:745.f})for(float y:{262.f,338.f})rect("Alça de seleção",a,b,x,y,5,5,white);
    cursor(a,b,637,306);scale(g,0,1.55f);scale(g,18,1);if(hera){key(title,TrackProperty::Opacity,0,0);key(title,TrackProperty::Opacity,9,1);}
}
int main(int argc,char**argv) {
    const bool full=argc>1 && std::string(argv[1])=="render";
    EngineConfig cfg;cfg.backend=new vk::Backend();cfg.disableAutosave=true;cfg.workerCount=2;cfg.defaultFontPath=root+"assets/Roboto-Regular.ttf";cfg.documentsDirectory=root;
    check(engine.initialize(cfg));check(engine.new_project(1024,576,30,nullptr));comp=engine.project()->timeline().composition(engine.project()->timeline().current());comp->set_duration(FrameIndex{513});comp->set_background({.028f,.028f,.03f,1});
    comp->motion_blur().enabled=true;comp->motion_blur().shutterAngle=150;comp->motion_blur().samples=8;
    // 00:00–00:01.6 — warm bloom and staggered type.
    auto intro=group("01 • Text to Motion Graphics?",0,48);background(0,48,ink);warmLight(0,48,true);
    float x=218;const char* phrase[]={"Text","to","Motion","Graphics?"};
    for(int i=0;i<4;i++){auto t=label(phrase[i],8+i*3,48,x+(i==2?71: i==3?98:42),288,46,white,true);entrance(t,12,10);x+=i==0?109:i==1?66:i==2?167:180;key(t,TrackProperty::Opacity,31-i*3,1);key(t,TrackProperty::Opacity,39-i*3,0);}
    auto light=rect("Curseur lumineux",8,44,197,286,4,36,{1,.8f,.25f,1},2);entrance(light,8);
    scale(intro,0,.95f);scale(intro,47,1.08f);
    prompt(48,94,false);
    // 00:03.13–00:04.7 — audio input card, all waveform bars editable.
    auto audioGroup=group("03 • Import de voix",94,141);background(94,141,ink);
    auto card=rect("Lecteur audio",94,141,512,291,402,430,{.105f,.103f,.102f,1},77);layer(card).shape.strokeWidth=1.5;layer(card).shape.strokeColor={.34f,.34f,.34f,1};
    for(int i=0;i<76;i++){
        const float h=8+165*std::pow(std::abs(std::sin(i*.317f)*std::cos(i*.173f)),1.6f);
        auto bar=rect("Onde audio "+std::to_string(i+1),94,141,330+i*4.8f,245,2.1f,h,i<24?Vec4{.78f,.1f,.2f,1}:white,1);
        key(bar,TrackProperty::ScaleY,0,.02f);key(bar,TrackProperty::ScaleY,11+i%7,1);key(bar,TrackProperty::ScaleY,29,.85f);key(bar,TrackProperty::ScaleY,42,1);
    }
    rect("Tête de lecture",94,141,444,248,2,196,{.3f,.59f,.99f,1});for(float y:{150.f,346.f})rect("Poignée bleue",94,141,444,y,14,14,{.3f,.59f,.99f,1},7,true);
    rect("Stop",94,141,404,413,78,78,{.3f,.3f,.3f,1},39,true);rect("Carré stop",94,141,404,413,17,17,white,4);
    rect("Pause",94,141,512,413,78,78,coral,39,true);for(float x:{505.f,519.f})rect("Pause barre",94,141,x,413,5,22,white,2);
    rect("Import",94,141,625,411,92,42,coral,14);label("Import",94,141,625,411,20,white);
    scale(audioGroup,0,.12f);scale(audioGroup,10,.94f);scale(audioGroup,15,1);scale(audioGroup,38,1);scale(audioGroup,46,1.3f);
    // 00:04.7–00:06 — voiceover headline on coral.
    auto voice=group("04 • Import your own voiceovers",141,183);background(141,183,coral);
    auto line=label("Import your own voiceovers",143,183,512,288,42,white,true);entrance(line,10,24);scale(voice,0,.94f);scale(voice,41,1);
    // 00:06–00:08.25 — select voice style, staggered pill stack.
    auto styles=group("05 • Select your desired style",183,249);background(183,249,white);
    auto select=label("Select your desired",183,216,512,286,45,ink,true);entrance(select,12,12);
    const char* labels[]={"Calm","Cinematic","Punchy","Corporate"};
    for(int i=0;i<4;i++){
        const int a=207+i*4;const float y=192+55.f*i;
        auto pill=rect(std::string("Style • ")+labels[i],a,249,583,y,236,49,coral,15);entrance(pill,12,55);
        auto t=label(labels[i],a,249,581,y,30,white);entrance(t,12,55);
    }
    scale(styles,0,.97f);scale(styles,65,1.04f);
    // 00:08.3–00:09.6 — two quick typographic beats.
    group("06 • Don't like it?",249,267);background(249,267,white);auto dislike=label("Don't like it?",249,267,512,288,47,ink,true);entrance(dislike,9,12);
    group("07 • Customize it",267,290);background(267,290,white);auto customize=label("Customize it",267,290,512,288,49,ink,true);entrance(customize,8,14);
    editor(290,332,false);prompt(332,384,true);editor(384,418,true);
    group("08 • Signature",418,432);background(418,432,white);label("karlozyx",418,432,512,288,49,ink,true);
    // Final Hera end card, stopping before the platform's branded outro.
    auto end=group("09 • Hera",432,513);background(432,513,coral);
    auto logo=svg("Hera • symbole vectoriel",432,513,"<g fill=\"#fff7ea\"><circle cx=\"380\" cy=\"274\" r=\"15\"/><circle cx=\"380\" cy=\"306\" r=\"15\"/><path d=\"M397 260Q425 255 425 282V299Q425 323 398 320V294L389 290Z\"/></g>");
    auto brand=label("Hera",432,513,526,292,77,{1,.974f,.919f,1});entrance(logo,10,6);entrance(brand,12,8);scale(end,0,.93f);scale(end,16,1);scale(end,80,1.01f);
    parent={};
    Asset sound;sound.kind=AssetKind::Audio;sound.name="Audio de la référence";sound.sourcePath=root+"assets/reference-audio.wav";sound.audio.sampleRate=48000;sound.audio.channels=2;sound.audio.sampleCount=FrameIndex{820800};sound.duration=FrameIndex{513};sound.timebaseFps=30;
    const auto asset=engine.project()->add_asset(sound);auto audio=create(LayerKind::Audio,sound.name,0,513);layer(audio).source=asset;
    engine.project()->mark_dirty();check(engine.save_project((root+"Hera-Motion-Windows.aurea").c_str()));
    check(engine.load_project((root+"Hera-Motion-Windows.aurea").c_str()));comp=engine.project()->timeline().composition(engine.project()->timeline().current());
    FILE* raw=full?fopen((root+"render.rgba").c_str(),"wb"):nullptr;
    const int probes[]={24,65,121,161,230,259,278,311,358,400,452,500};
    for(int frame=0;frame<513;frame++){
        bool probe=std::find(std::begin(probes),std::end(probes),frame)!=std::end(probes);if(!full&&!probe)continue;
        Command seek{};seek.type=CommandType::PlaybackSeek;seek.seek.time=tick_at(FrameIndex{frame},30);check(engine.apply_command(seek));
        std::vector<u8> pixels;u32 w,h;check(engine.capture_frame_rgba(1024,pixels,w,h));
        if(raw&&fwrite(pixels.data(),1,pixels.size(),raw)!=pixels.size())return 5;
        if(probe){char name[80];sprintf(name,"frame-%03d.png",frame);test::write_png(root+name,test::Image8{w,h,pixels});fprintf(stderr,"FRAME %d\n",frame);}
    }
    if(raw)fclose(raw);
    engine.project()->for_each_asset([](AssetId,Asset& a){if(a.sourcePath.rfind(root,0)==0)a.sourcePath="docs:"+a.sourcePath.substr(root.size());});
    comp->layers().for_each([](LayerId,Layer& l){if(l.kind==LayerKind::Text)l.text.fontPath="docs:assets/Roboto-Regular.ttf";});
    engine.project()->timeline().set_playhead(FrameIndex{0});check(engine.save_project((root+"Hera-Motion-Portable.aurea").c_str()));engine.shutdown();
}
