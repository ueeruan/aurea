#include "aurea/export/FFmpegExportSink.hpp"
// Windows host. Editing, interpolation, rendering and persistence stay in aurea_core.
#include "WindowsMedia.hpp"
#include "TimelineGeometry.hpp"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <gdiplus.h>
// VulkanLoader removes legacy near/far macros after the Windows SDK headers.
#include "VulkanBackend.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <cstdio>

using namespace aurea;
using namespace aurea::windows;
namespace fs=std::filesystem;
int run_desktop_tests(const std::wstring& directory);
namespace {
// Original AUREA chrome. Neutral panels keep footage authoritative; blue means focus/time.
constexpr COLORREF BG=RGB(22,22,24), PANEL=RGB(33,33,36), FIELD=RGB(25,25,28), LINE=RGB(57,57,62), FG=RGB(228,228,232), MUTED=RGB(163,163,174), ACCENT=RGB(89,174,255), SELECTED=RGB(48,66,85), HEADER=RGB(39,39,43);
constexpr UINT WM_TASK_DONE=WM_APP+1;
enum Action {New=100,Open,Save,SaveAs,Import,Export,Png,Quit,Undo,Redo,Duplicate,Delete,Text,Rectangle,Ellipse,Cube,Split,Play,Marker,Prev,Next,ZoomIn,ZoomOut,Reset,Help,Settings,AddEffect,RemoveEffect,ToggleEffect,Color,TextApply,Raise,Lower,AutoKey,LinkScale,Expand,CancelExport,Graph,WorkspaceCompose,WorkspaceEdit,InspectorTransform,InspectorEffects};
constexpr int Props[]={0,1,2,3,4,5,6,7,8,12};
const wchar_t* PropNames[]={L"Posição X",L"Posição Y",L"Posição Z",L"Escala X",L"Escala Y",L"Escala Z",L"Rotação X",L"Rotação Y",L"Rotação Z",L"Opacidade"};
struct Box {float x,y,w,h;bool contains(float px,float py)const{return px>=x&&px<x+w&&py>=y&&py<y+h;}};
struct Hit {Box rect;int action;u64 layer=0;bridge::KeyframeRow key{};int row=0;};
struct TimelineRow {u64 layer;int index;int property=-1;u32 effect=kInvalidIndex,param=0;std::wstring label;};
struct CatalogItem {u32 type;std::wstring name;};
std::wstring window_text(HWND w){const int n=GetWindowTextLengthW(w);std::wstring s(n+1,0);GetWindowTextW(w,s.data(),n+1);s.resize(n);return s;}
 void control_text(HWND w,const std::wstring& text){if(window_text(w)!=text)SetWindowTextW(w,text.c_str());}
std::wstring number(double v,int digits=1){std::wostringstream o;o<<std::fixed<<std::setprecision(digits)<<v;return o.str();}
std::wstring timecode(i64 frame,double fps){const i64 f=std::max<i64>(1,std::llround(fps));frame=std::max<i64>(0,frame);wchar_t s[64];swprintf_s(s,L"%02lld:%02lld:%02lld:%02lld",frame/f/3600,(frame/f/60)%60,(frame/f)%60,frame%f);return s;}
bool numeric(const std::wstring& text,double& out){wchar_t* end=nullptr;out=wcstod(text.c_str(),&end);while(end&&iswspace(*end))++end;return end&&end!=text.c_str()&&!*end&&std::isfinite(out);}
struct App {
 HWND window=nullptr,preview=nullptr,search=nullptr,catalog=nullptr,assets=nullptr,effects=nullptr,paramList=nullptr,paramValue=nullptr,textBox=nullptr,scrollbar=nullptr;
 std::array<HWND,10> fields{},keys{},labels{};std::map<int,HWND> buttons;HFONT font=nullptr,headingFont=nullptr,monoFont=nullptr;HWND tooltips=nullptr;HBRUSH fieldBrush=CreateSolidBrush(FIELD),panelBrush=CreateSolidBrush(PANEL);
 Engine engine;MediaFactory media;std::unique_ptr<audio::AudioOutput> audio;EngineStatus status{};bridge::LayerDetailPOD detail{};
 std::vector<bridge::LayerRow> layers;std::string names;std::map<u64,std::vector<bridge::KeyframeRow>> keyframes;
 std::vector<TimelineRow> rows;std::vector<Hit> hits;std::vector<CatalogItem> library,filtered;
 std::vector<bridge::LayerEffectRow> applied;std::vector<bridge::EffectParamRow> params;std::string paramNames;
 fs::path appDirectory,projectPath;std::wstring title=L"Sem título",notice=L"Crie uma camada ou importe sua mídia para começar.";
 float dpi=1,width=1400,height=850,left=260,right=300,timelineHeight=330;int rowScroll=0;TimelineGeometry geometry{320,0,3};
 u64 selected=0;u32 revision=~0u,activeEffect=kInvalidIndex,activeParam=0;int activeProperty=0;bool ready=false,busy=false,autoKey=false,linked=true,expanded=true,graph=false,updating=false,editing=false,inspectorFx=false;
 int drag=0;POINT down{};Hit dragHit{};i64 dragFrame=0,dragStart=0,dragEnd=0,dragOffset=0;float dragX=0,dragY=0;double dragScroll=0;bool draggingCommands=false;
 std::future<void> task;std::mutex taskMutex;Status taskStatus{};u64 taskLayer=0;std::wstring taskNotice;bool taskChanged=false;bool exportWasRunning=false;
 u32 selectedEffect=kInvalidIndex;Box canvas{};bool smoke=false;int smokeTicks=0;fs::path smokeDirectory;
 ~App(){if(task.valid())task.wait();engine.stop_render_thread();engine.detach_surface();engine.shutdown();if(font)DeleteObject(font);if(headingFont)DeleteObject(headingFont);if(monoFont)DeleteObject(monoFont);DeleteObject(fieldBrush);DeleteObject(panelBrush);}
 int px(float value)const{return int(std::lround(value*dpi));}
 void updateFonts(){
  auto previous=font,previousHeading=headingFont,previousMono=monoFont;
  font=CreateFontW(-px(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  headingFont=CreateFontW(-px(12),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  monoFont=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Consolas");
  EnumChildWindows(window,[](HWND w,LPARAM f)->BOOL{SendMessageW(w,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},reinterpret_cast<LPARAM>(font));
  if(previous)DeleteObject(previous);if(previousHeading)DeleteObject(previousHeading);if(previousMono)DeleteObject(previousMono);
 }
 void invalidate(){InvalidateRect(window,nullptr,FALSE);}
 void message(const std::wstring& s){notice=s;invalidate();}
 void error(Status s){if(!s.ok())message(L"Não foi possível concluir: "+wide(std::string(s.message())));}
 void send(Command c,const std::string& text={}) {if(!ready||busy)return;c.stringLength=u32(text.size());if(engine.submit_commands(&c,1,text.empty()?nullptr:text.data(),u32(text.size()))!=1)message(L"A fila de edição está ocupada. Tente novamente.");engine.request_render();}
 void command(CommandType type){Command c;c.type=type;send(c);}
 void seekFrame(i64 frame){Command c;c.type=CommandType::PlaybackSeek;c.seek.time=tick_at(FrameIndex{std::clamp<i64>(frame,0,std::max<i64>(0,status.duration.value-1))},status.compFps);send(c);}
 LayerId layerId()const{return LayerId::unpack(selected);}
 float propertyValue(int p)const {if(p<3)return detail.position[p];if(p<6)return detail.scale[p-3];if(p<9)return detail.rotation[p-6];return detail.opacity;}
 bridge::LayerRow* layer(u64 id){for(auto& l:layers)if(l.id==id)return &l;return nullptr;}
 std::wstring layerName(const bridge::LayerRow& l)const {return l.nameOffset+l.nameLength<=names.size()?wide(names.substr(l.nameOffset,l.nameLength)):L"Camada";}
 void select(u64 id){selected=id;engine.set_selection(&id,1);selectedEffect=kInvalidIndex;activeEffect=kInvalidIndex;activeParam=0;activeProperty=0;revision=~0u;refresh();}
 void beginGroup(){if(!draggingCommands){command(CommandType::UndoBeginGroup);draggingCommands=true;}}
 void endGroup(){if(draggingCommands){command(CommandType::UndoEndGroup);draggingCommands=false;}}
 void setProperty(int p,float value,bool group=false) {
  if(!selected||!std::isfinite(value))return;
  const bool animate=autoKey||(detail.animatedMask&(1u<<p));Command c;
  if(animate){c.type=CommandType::KeyframeInsert;c.keyframe={TrackRef{layerId(),static_cast<TrackProperty>(p),kInvalidIndex,0},FrameIndex{detail.localPlayhead},value};}
  else {c.type=CommandType::LayerLayoutTransform;c.shape_param={layerId(),u32(p),value};}
  if(!group)command(CommandType::UndoBeginGroup);send(c);
  if(linked&&p>=3&&p<=5){const float previous=propertyValue(p);const float ratio=std::abs(previous)>1e-6f?value/previous:1.f;for(int q=3;q<=5;++q)if(q!=p){const float v=std::abs(previous)>1e-6f?propertyValue(q)*ratio:value;Command other;if(autoKey||(detail.animatedMask&(1u<<q))){other.type=CommandType::KeyframeInsert;other.keyframe={TrackRef{layerId(),static_cast<TrackProperty>(q),kInvalidIndex,0},FrameIndex{detail.localPlayhead},v};}else{other.type=CommandType::LayerLayoutTransform;other.shape_param={layerId(),u32(q),v};}send(other);}}
  if(!group)command(CommandType::UndoEndGroup);
 }
 void addKey(int property){if(!selected)return;activeProperty=property;activeEffect=kInvalidIndex;activeParam=0;Command c;c.type=CommandType::KeyframeInsert;c.keyframe={TrackRef{layerId(),static_cast<TrackProperty>(property),kInvalidIndex,0},FrameIndex{detail.localPlayhead},propertyValue(property)};send(c);expanded=true;}
 void run(std::function<void()> fn){if(busy)return;busy=true;taskStatus=OkStatus;taskLayer=0;taskNotice=L"Concluído.";taskChanged=false;if(task.valid())task.get();task=std::async(std::launch::async,[this,fn=std::move(fn)]{fn();PostMessageW(window,WM_TASK_DONE,0,0);});message(L"Processando…");}
 void finishTask(){if(task.valid())task.get();busy=false;if(taskStatus.ok()){if(taskLayer)select(taskLayer);message(taskNotice);}else error(taskStatus);revision=~0u;refresh();}
 std::wstring choose(bool save,const wchar_t* filter,const wchar_t* ext,const std::wstring& initial={}) {
  wchar_t file[32768]{};wcsncpy_s(file,initial.c_str(),_TRUNCATE);OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=window;ofn.lpstrFilter=filter;ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.lpstrDefExt=ext;
  ofn.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);return (save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn))?file:L"";
 }
 bool saveProject(bool as=false){if(busy)return false;const auto focused=GetFocus();if(focused==paramValue||std::find(fields.begin(),fields.end(),focused)!=fields.end())SetFocus(window);std::wstring path=projectPath.wstring();if(path.empty()||as)path=choose(true,L"Projeto AUREA (*.aurea)\0*.aurea\0\0",L"aurea",title+L".aurea");if(path.empty())return false;
  const auto result=engine.save_project(utf8(path).c_str());if(!result.ok()){error(result);return false;}projectPath=path;title=projectPath.stem().wstring();message(L"Projeto salvo. Mantenha os arquivos de mídia nas pastas de origem.");return true;
 }
 bool mayDiscard(){if(busy){message(L"Aguarde a operação terminar.");return false;}if(engine.export_progress().running){message(L"Aguarde a exportação ou cancele-a.");return false;}if(!engine.read_status().dirty)return true;const int answer=MessageBoxW(window,L"Salvar as alterações antes de continuar?",L"AUREA",MB_YESNOCANCEL|MB_ICONQUESTION);return answer==IDNO||(answer==IDYES&&saveProject());}
 void importFile(const fs::path& path) {
  if(busy)return;auto ext=path.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
  if(ext==L".aurea"){if(!mayDiscard())return;run([this,path]{taskStatus=engine.load_project(utf8(path.wstring()).c_str());if(taskStatus.ok()){projectPath=path;title=path.stem().wstring();taskNotice=engine.last_load_notice()?L"Projeto aberto com aviso: confira as mídias ausentes ou recuperadas.":L"Projeto aberto.";}});return;}
  run([this,path,ext]{const auto p=utf8(path.wstring()),name=utf8(path.stem().wstring());Result<u64> result{Status{Errc::UnsupportedFormat}};
   if(ext==L".glb"||ext==L".gltf"||ext==L".fbx"||ext==L".obj"){ModelImport r;r.path=p;r.displayName=name;result=engine.import_model(r);}
   else if(ext==L".svg"){std::ifstream f(path);std::string data((std::istreambuf_iterator<char>(f)),{});result=engine.import_svg(data,name.c_str());}
   else if(ext==L".png"||ext==L".jpg"||ext==L".jpeg"||ext==L".bmp"||ext==L".tif"||ext==L".tiff"||ext==L".webp"){ImagePixels img;if(load_image(p.c_str(),img,nullptr))result=engine.import_image(img.rgba.data(),img.width,img.height,name.c_str(),p.c_str());else result=Status{Errc::UnsupportedFormat,"Imagem nao reconhecida pelo Windows"};}
   else {MediaProbe probe;if(media.probe(p.c_str(),probe)){VideoImport r;r.sourcePath=p;r.displayName=name;result=probe.hasVideo?engine.import_video(r):engine.import_audio(r);}else result=Status{Errc::UnsupportedCodec,"Midia indisponivel: use video SDR H.264 ou audio compativel com o Windows"};}
   taskStatus=result.status();if(result.ok())taskLayer=*result;taskNotice=L"Importado: "+path.filename().wstring();
  });
 }
 void exportVideo(){if(busy||engine.export_progress().running)return;const auto path=choose(true,L"Vídeo MP4 H.264 (*.mp4)\0*.mp4\0\0",L"mp4",title+L".mp4");if(path.empty())return;ExportSettings settings;settings.height=std::min(status.compWidth,status.compHeight);settings.width=status.compWidth;settings.fps=status.compFps;const auto result=engine.start_export(settings,utf8(path).c_str());if(result.ok()){exportWasRunning=true;message(L"Exportando MP4…");}else error(result);}
 HWND control(const wchar_t* cls,const wchar_t* label,DWORD style,int id){auto w=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|(wcscmp(cls,L"STATIC")?WS_TABSTOP:0)|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(w,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return w;}
 HWND button(int action,const wchar_t* label){auto w=control(L"BUTTON",label,BS_OWNERDRAW,action);buttons[action]=w;return w;}
 void place(HWND w,float x,float y,float a,float b){MoveWindow(w,px(x),px(y),px(std::max(1.f,a)),px(std::max(1.f,b)),TRUE);}
 float rowHeight()const{return editing?38.f:27.f;}
 void makeControls();void layout();void refresh();void paint(HDC dc);void action(int id);void pointerDown(float x,float y);void pointerMove(float x,float y);void pointerUp();void context(float x,float y);void key(WPARAM code);void updateEffects();void updateParams();void filterEffects();void timer();
};
App* app=nullptr;
LRESULT CALLBACK FieldProc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
 if(msg==WM_GETDLGCODE&&wp==VK_RETURN)return DLGC_WANTALLKEYS;
 if(msg==WM_KEYDOWN&&wp==VK_RETURN){SetFocus(app->window);return 0;}
 if(msg==WM_KEYDOWN&&wp==VK_ESCAPE){app->revision=~0u;SetFocus(app->window);app->refresh();return 0;}
 return DefSubclassProc(w,msg,wp,lp);
}
LRESULT CALLBACK PreviewProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
 if(!app)return DefWindowProcW(w,msg,wp,lp);
 if(msg==WM_ERASEBKGND)return 1;
 if(msg==WM_LBUTTONDOWN||msg==WM_LBUTTONDBLCLK){SetFocus(app->window);if(!app->selected)return 0;app->drag=7;app->down={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};app->dragX=app->detail.position[0];app->dragY=app->detail.position[1];app->beginGroup();SetCapture(w);return 0;}
 if(msg==WM_MOUSEMOVE&&app->drag==7&&(wp&MK_LBUTTON)) {RECT r;GetClientRect(w,&r);const float scale=std::min(float(r.right)/app->status.compWidth,float(r.bottom)/app->status.compHeight);if(scale>0){app->setProperty(0,app->dragX+(GET_X_LPARAM(lp)-app->down.x)/scale,true);app->setProperty(1,app->dragY+(GET_Y_LPARAM(lp)-app->down.y)/scale,true);}return 0;}
 if(msg==WM_LBUTTONUP&&app->drag==7){app->pointerUp();return 0;}
 if(msg==WM_SIZE&&app->ready&&LOWORD(lp)&&HIWORD(lp)){(void)app->engine.resize_surface(LOWORD(lp),HIWORD(lp));app->engine.request_render();return 0;}
 return DefWindowProcW(w,msg,wp,lp);
}
void App::makeControls(){
 dpi=GetDpiForWindow(window)/96.f;updateFonts();
 button(Import,L"Importar");button(Text,L"Texto");button(Rectangle,L"Forma");button(Cube,L"3D");button(Undo,L"Desfazer");button(Redo,L"Refazer");button(Export,L"Exportar");
 button(WorkspaceCompose,L"Composição");button(WorkspaceEdit,L"Edição");button(InspectorTransform,L"Transformar");button(InspectorEffects,L"Efeitos");
 button(Play,L"Reproduzir (Espaço)");button(Marker,L"+ Marca");button(Prev,L"Marca ou keyframe anterior");button(Next,L"Próxima marca ou keyframe");button(ZoomOut,L"Reduzir timeline");button(ZoomIn,L"Ampliar timeline");button(AutoKey,L"Auto-key: OFF");button(LinkScale,L"Escala vinculada: ON");button(Expand,L"Keyframes");button(Graph,L"Editor de gráfico");
 button(AddEffect,L"+ Aplicar efeito");button(RemoveEffect,L"Remover");button(ToggleEffect,L"Ligar / desligar");button(Color,L"Cor");button(TextApply,L"Aplicar texto");
 search=control(L"EDIT",L"",ES_AUTOHSCROLL,400);SendMessageW(search,EM_SETCUEBANNER,0,reinterpret_cast<LPARAM>(L"Buscar efeitos…"));
 catalog=control(L"LISTBOX",L"Catálogo de efeitos",LBS_NOTIFY|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,401);
 assets=control(L"LISTBOX",L"Projeto e camadas",LBS_NOTIFY|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,402);
 effects=control(L"COMBOBOX",L"Efeitos da camada",CBS_DROPDOWNLIST|WS_VSCROLL,403);
 paramList=control(L"COMBOBOX",L"Parâmetro do efeito",CBS_DROPDOWNLIST|WS_VSCROLL,404);
 paramValue=control(L"EDIT",L"",ES_AUTOHSCROLL,405);SetWindowSubclass(paramValue,FieldProc,1,0);
 button(406,L"Inserir keyframe do parâmetro");
 for(int i=0;i<10;++i){labels[i]=control(L"STATIC",PropNames[i],SS_LEFT,700+i);fields[i]=control(L"EDIT",L"",ES_AUTOHSCROLL,500+i);keys[i]=control(L"BUTTON",(L"Inserir keyframe: "+std::wstring(PropNames[i])).c_str(),BS_OWNERDRAW,600+i);SetWindowSubclass(fields[i],FieldProc,1,0);}
 textBox=control(L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN,410);
 scrollbar=control(L"SCROLLBAR",L"Tempo",SBS_HORZ,411);
 WNDCLASSW wc{};wc.lpfnWndProc=PreviewProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AureaComposition";wc.style=CS_DBLCLKS;wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);RegisterClassW(&wc);
 preview=CreateWindowExW(0,wc.lpszClassName,L"Composição AUREA",WS_CHILD|WS_VISIBLE,0,0,1,1,window,nullptr,wc.hInstance,nullptr);
 tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,wc.hInstance,nullptr);
 for(auto [id,w]:buttons){TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(w);info.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}
 for(HWND w:keys){TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=window;info.uId=reinterpret_cast<UINT_PTR>(w);info.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}
}
void App::layout(){
 RECT r;GetClientRect(window,&r);width=r.right/dpi;height=r.bottom/dpi;
 left=std::clamp(left,210.f,std::max(210.f,width*.28f));right=std::clamp(right,272.f,std::max(272.f,width*.3f));timelineHeight=std::clamp(timelineHeight,200.f,std::max(200.f,height-515));
 const float bottom=height-timelineHeight,rx=width-right;canvas={left+5,88,width-left-right-10,bottom-128};
 float x=12;for(auto pair:std::vector<std::pair<int,int>>{{Import,80},{Text,58},{Rectangle,62},{Cube,44},{Undo,70},{Redo,66}}){place(buttons[pair.first],x,7,float(pair.second),28);x+=pair.second+4;}
 place(buttons[WorkspaceCompose],width-340,7,108,28);place(buttons[WorkspaceEdit],width-228,7,76,28);place(buttons[Export],width-112,7,100,28);
 place(preview,canvas.x,canvas.y,canvas.w,canvas.h);
 place(assets,12,editing?bottom+105:123,left-24,editing?timelineHeight-150:std::max(72.f,(bottom-175)*.36f));
 const float libY=editing?345:123+std::max(72.f,(bottom-175)*.36f)+39;
 place(search,12,libY,left-24,25);place(catalog,12,libY+32,left-24,std::max(40.f,bottom-libY-83));place(buttons[AddEffect],12,bottom-42,left-24,28);
 place(buttons[InspectorTransform],rx+12,83,editing?right-24:(right-28)*.58f,25);place(buttons[InspectorEffects],rx+16+(right-28)*.58f,83,(right-28)*.42f,25);ShowWindow(buttons[InspectorEffects],editing?SW_HIDE:SW_SHOW);
 const float col=(right-24)/3;
 for(int i=0;i<10;++i){const float x0=rx+12+(i%3)*col,y0=151+(i/3)*49;place(labels[i],x0,y0-19,col-2,17);place(fields[i],x0,y0,col-27,24);place(keys[i],x0+col-26,y0,22,24);}
 place(buttons[LinkScale],rx+12,324,right-68,24);place(buttons[Color],width-51,324,39,24);
 place(textBox,rx+12,389,right-24,60);place(buttons[TextApply],rx+12,457,right-24,25);
 const float fxX=editing?12:rx+12,fxW=editing?left-24:right-24,fxY=editing?120:148;
 place(effects,fxX,fxY,fxW,200);place(paramList,fxX,fxY+69,fxW,220);place(paramValue,fxX,fxY+134,fxW-38,25);place(buttons[406],fxX+fxW-26,fxY+134,26,25);
 place(buttons[ToggleEffect],fxX,fxY+175,fxW-84,26);place(buttons[RemoveEffect],fxX+fxW-76,fxY+175,76,26);
 const float ty=bottom+38,tx=editing?left+8:12;
 place(buttons[Prev],tx,ty,32,26);place(buttons[Play],tx+37,ty,38,26);place(buttons[Next],tx+80,ty,32,26);place(buttons[Marker],tx+120,ty,77,26);place(buttons[AutoKey],tx+206,ty,110,26);
 place(buttons[Expand],tx+328,ty,88,26);place(buttons[Graph],tx+424,ty,122,26);place(buttons[ZoomOut],width-79,ty,28,26);place(buttons[ZoomIn],width-45,ty,28,26);
 geometry.origin=editing?left+192:std::max(280.f,left+32);place(scrollbar,float(geometry.origin),height-39,width-float(geometry.origin)-10,14);
 if(ready){engine.invalidate();revision=~0u;refresh();}invalidate();
}
void App::filterEffects(){filtered.clear();const auto query=window_text(search);auto lower=[](std::wstring s){std::transform(s.begin(),s.end(),s.begin(),::towlower);return s;};const auto q=lower(query);SendMessageW(catalog,LB_RESETCONTENT,0,0);for(auto& c:library)if(q.empty()||lower(c.name).find(q)!=std::wstring::npos){filtered.push_back(c);SendMessageW(catalog,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(c.name.c_str()));}if(!filtered.empty())SendMessageW(catalog,LB_SETCURSEL,0,0);}
void App::updateEffects(){
 const auto previous=selectedEffect;std::array<bridge::LayerEffectRow,256> fx{};std::array<char,65536> blob{};const auto count=engine.query_layer_effects(selected,fx.data(),u32(fx.size()),blob.data(),u32(blob.size()));applied.assign(fx.begin(),fx.begin()+std::min<size_t>(count,fx.size()));
 SendMessageW(effects,CB_RESETCONTENT,0,0);int chosen=0;for(size_t i=0;i<applied.size();++i){const auto& f=applied[i];auto text=wide(std::string(blob.data()+f.nameOffset,f.nameLength));if(!f.enabled)text=L"[OFF] "+text;SendMessageW(effects,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(f.effectId==previous)chosen=int(i);}
 selectedEffect=applied.empty()?kInvalidIndex:applied[chosen].effectId;SendMessageW(effects,CB_SETCURSEL,chosen,0);updateParams();
}
void App::updateParams(){
 const int old=std::max(0,int(SendMessageW(paramList,CB_GETCURSEL,0,0)));std::array<bridge::EffectParamRow,256> p{};paramNames.assign(65536,0);
 const auto count=engine.query_effect_params(selected,selectedEffect,p.data(),u32(p.size()),paramNames.data(),u32(paramNames.size()));params.assign(p.begin(),p.begin()+std::min<size_t>(count,p.size()));
 SendMessageW(paramList,CB_RESETCONTENT,0,0);for(auto& entry:params){auto text=wide(paramNames.substr(entry.labelOffset,entry.labelLength));SendMessageW(paramList,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
 const int chosen=std::min(old,std::max(0,int(params.size())-1));SendMessageW(paramList,CB_SETCURSEL,chosen,0);if(!params.empty()&&GetFocus()!=paramValue)SetWindowTextW(paramValue,number(params[chosen].value[0],3).c_str());
}
void App::refresh(){
 if(!ready||busy)return;const auto previousFrame=status.playhead.value;status=engine.read_status();
 if(status.modelRevision!=revision){revision=status.modelRevision;
  const u32 capacity=std::max(16u,status.layerCount+16);layers.resize(capacity);names.assign(std::max<size_t>(65536,size_t(capacity)*512),0);
  const auto count=engine.query_layers(layers.data(),capacity,names.data(),u32(names.size()));layers.resize(std::min(count,capacity));
  u64 selection=0;if(engine.get_selection(&selection,1)>0)selected=selection;else selected=0;
  keyframes.clear();for(auto& l:layers){auto& list=keyframes[l.id];list.resize(l.keyframeCount+16);const u32 n=engine.query_keyframes(l.id,list.data(),u32(list.size()));if(n>list.size()){list.resize(n);engine.query_keyframes(l.id,list.data(),n);}else list.resize(n);}
  SendMessageW(assets,LB_RESETCONTENT,0,0);for(size_t i=0;i<layers.size();++i){auto& l=layers[i];const auto tag=l.kind==u32(LayerKind::Audio)?L"A  ":l.kind==u32(LayerKind::Text)?L"T  ":l.flags&bridge::kLayerRowFlagThreeD?L"3D  ":L"V  ";const auto label=std::wstring(tag)+layerName(l);SendMessageW(assets,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(l.id==selected)SendMessageW(assets,LB_SETCURSEL,i,0);}
  if(selected){engine.query_layer_detail(selected,detail);updateEffects();TextData text;if(engine.query_text(selected,text)&&GetFocus()!=textBox)SetWindowTextW(textBox,wide(text.content).c_str());}
 }
 if(selected)engine.query_layer_detail(selected,detail);else detail={};
 updating=true;
 for(int i=0;i<10;++i){EnableWindow(fields[i],selected!=0);EnableWindow(keys[i],selected!=0);if(GetFocus()!=fields[i]){const int p=Props[i];const double value=propertyValue(p)*(p>=3&&p<=5||p==12?100:1);control_text(fields[i],number(value));}const auto accessible=(detail.keyAtPlayheadMask&(1u<<Props[i])?L"Keyframe neste quadro: ":L"Inserir keyframe: ")+std::wstring(PropNames[i]);control_text(keys[i],accessible);for(HWND w:{fields[i],keys[i],labels[i]})ShowWindow(w,inspectorFx?SW_HIDE:SW_SHOW);}
 for(HWND w:{buttons[LinkScale],buttons[Color]}){ShowWindow(w,inspectorFx?SW_HIDE:SW_SHOW);EnableWindow(w,selected!=0);}
 const bool showText=selected&&detail.kind==static_cast<u32>(LayerKind::Text)&&!inspectorFx;
 ShowWindow(textBox,showText?SW_SHOW:SW_HIDE);ShowWindow(buttons[TextApply],showText?SW_SHOW:SW_HIDE);
 for(HWND w:{effects,paramList,paramValue,buttons[406],buttons[ToggleEffect],buttons[RemoveEffect]}){ShowWindow(w,inspectorFx||editing?SW_SHOW:SW_HIDE);EnableWindow(w,selected&&!applied.empty());}
 EnableWindow(buttons[AddEffect],selected!=0);EnableWindow(buttons[TextApply],showText);EnableWindow(buttons[Graph],selected!=0);
 if(previousFrame!=status.playhead.value&&selectedEffect!=kInvalidIndex&&GetFocus()!=paramValue)updateParams();updating=false;
 rows.clear();for(size_t i=0;i<layers.size();++i){auto& l=layers[i];rows.push_back({l.id,int(i),-1,kInvalidIndex,0,layerName(l)});if(expanded&&l.id==selected){std::set<std::tuple<u32,u32,u32>> seen;for(auto& k:keyframes[l.id])if(seen.emplace(k.property,k.effectIndex,k.paramIndex).second){std::wstring label=L"Propriedade "+std::to_wstring(k.property);for(int p=0;p<10;++p)if(Props[p]==int(k.property))label=PropNames[p];if(k.property==u32(TrackProperty::EffectParam))label=L"Efeito / parâmetro "+std::to_wstring(k.paramIndex/4+1);rows.push_back({l.id,int(i),int(k.property),k.effectIndex,k.paramIndex,label});}}}
 const int visible=std::max(1,int((timelineHeight-148)/rowHeight()));rowScroll=std::clamp(rowScroll,0,std::max(0,int(rows.size())-visible));
 SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS};si.nMax=int(std::min<i64>(status.duration.value+120,INT_MAX));si.nPage=UINT(std::max(1.,(width-geometry.origin)/geometry.pixelsPerFrame));si.nPos=int(geometry.scrollFrames);SetScrollInfo(scrollbar,SB_CTL,&si,TRUE);
 const std::wstring caption=title+(status.dirty?L" *":L"")+L"  —  AUREA PC 0.0.1 · Beta teste";control_text(window,caption);control_text(buttons[Play],status.playing?L"Pausar (Espaço)":L"Reproduzir (Espaço)");invalidate();
}
struct Painter {
 HDC dc;float scale;HFONT font;Painter(HDC d,float s,HFONT f):dc(d),scale(s),font(f){SetBkMode(dc,TRANSPARENT);SelectObject(dc,font);}
 RECT rect(Box b){return {LONG(std::lround(b.x*scale)),LONG(std::lround(b.y*scale)),LONG(std::lround((b.x+b.w)*scale)),LONG(std::lround((b.y+b.h)*scale))};}
 void fill(Box b,COLORREF c){RECT r=rect(b);auto brush=CreateSolidBrush(c);FillRect(dc,&r,brush);DeleteObject(brush);}
 void text(const std::wstring& t,Box b,COLORREF c=FG,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){auto r=rect(b);SetTextColor(dc,c);DrawTextW(dc,t.c_str(),int(t.size()),&r,flags);}
 void line(float x,float y,float x2,float y2,COLORREF c){auto p=CreatePen(PS_SOLID,1,c);auto old=SelectObject(dc,p);MoveToEx(dc,int(x*scale),int(y*scale),nullptr);LineTo(dc,int(x2*scale),int(y2*scale));SelectObject(dc,old);DeleteObject(p);}
 void diamond(float x,float y,COLORREF c,bool filled=true){POINT points[]={{LONG(x*scale),LONG((y-5)*scale)},{LONG((x+5)*scale),LONG(y*scale)},{LONG(x*scale),LONG((y+5)*scale)},{LONG((x-5)*scale),LONG(y*scale)}};auto brush=filled?CreateSolidBrush(c):static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));auto old=SelectObject(dc,brush);auto pen=CreatePen(PS_SOLID,1,c);auto oldPen=SelectObject(dc,pen);Polygon(dc,points,4);SelectObject(dc,old);SelectObject(dc,oldPen);DeleteObject(pen);if(filled)DeleteObject(brush);}
 void outline(Box b,COLORREF c){line(b.x,b.y,b.x+b.w,b.y,c);line(b.x+b.w,b.y,b.x+b.w,b.y+b.h,c);line(b.x+b.w,b.y+b.h,b.x,b.y+b.h,c);line(b.x,b.y+b.h,b.x,b.y,c);}
 void arrow(float x,float y,COLORREF c){POINT points[]={{LONG((x-5)*scale),LONG(y*scale)},{LONG((x+5)*scale),LONG(y*scale)},{LONG((x+5)*scale),LONG((y+7)*scale)},{LONG(x*scale),LONG((y+12)*scale)},{LONG((x-5)*scale),LONG((y+7)*scale)}};auto brush=CreateSolidBrush(c);auto old=SelectObject(dc,brush);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));Polygon(dc,points,5);SelectObject(dc,old);SelectObject(dc,pen);DeleteObject(brush);}
 void eye(float x,float y,bool visible){auto pen=CreatePen(PS_SOLID,1,visible?FG:MUTED);auto old=SelectObject(dc,pen);auto brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));::Ellipse(dc,LONG((x-7)*scale),LONG((y-4)*scale),LONG((x+7)*scale),LONG((y+4)*scale));if(visible){SelectObject(dc,GetStockObject(WHITE_BRUSH));::Ellipse(dc,LONG((x-2)*scale),LONG((y-2)*scale),LONG((x+2)*scale),LONG((y+2)*scale));}else line(x-7,y+5,x+7,y-5,MUTED);SelectObject(dc,old);SelectObject(dc,brush);DeleteObject(pen);}
};
void App::paint(HDC dc){
 Painter p(dc,dpi,font);const float bottom=height-timelineHeight,rx=width-right,origin=float(geometry.origin),rowX=editing?left+5:0;
 p.fill({0,0,width,height},BG);hits.clear();
 p.fill({0,43,left,bottom-43},PANEL);p.fill({rx,43,right,bottom-43},PANEL);p.fill({0,bottom,width,timelineHeight},PANEL);p.fill({0,0,width,42},HEADER);
 p.line(0,42,width,42,LINE);p.line(left,43,left,height-24,LINE);p.line(rx,43,rx,bottom,LINE);p.line(0,bottom,width,bottom,LINE);
 auto heading=[&](const std::wstring& text,float x,float y,float w){p.fill({x,y,w,33},HEADER);SelectObject(dc,headingFont);p.text(text,{x+12,y,w-24,32});SelectObject(dc,font);p.line(x,y+32,x+w,y+32,LINE);p.line(x+12,y+31,x+85,y+31,ACCENT);};
 heading(editing?L"Controles de efeito":L"Projeto",0,43,left);
 heading((editing?L"Programa: ":L"Composição: ")+title,left+5,43,width-left-right-10);
 heading(L"Propriedades",rx,43,right);
 p.outline(canvas,LINE);
 if(editing){p.text(selected&&layer(selected)?layerName(*layer(selected)):L"Selecione uma camada",{12,82,left-24,24},MUTED);heading(L"Projeto",0,bottom,left);p.text(title,{12,bottom+38,left-24,24});p.text(std::to_wstring(layers.size())+L" itens na composição",{12,bottom+66,left-24,24},MUTED);}
 else{p.text(title,{13,85,left-26,22});p.text(std::to_wstring(layers.size())+L" camadas",{13,106,left-26,16},MUTED);}
 const float libY=editing?345:123+std::max(72.f,(bottom-175)*.36f)+39;
 heading(L"Efeitos e predefinições",0,libY-34,left);
 p.text(std::to_wstring(status.compWidth)+L" × "+std::to_wstring(status.compHeight)+L"   |   "+number(status.compFps,2)+L" fps",{left+16,bottom-34,canvas.w*.65f,24},MUTED);
 p.text(L"Ajustar ao painel",{left+canvas.w*.65f,bottom-34,canvas.w*.35f-10,24},MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
 if(!inspectorFx){p.text(L"Transformação",{rx+12,110,right-24,20},MUTED);p.line(rx+12,359,width-12,359,LINE);if(selected&&detail.kind==u32(LayerKind::Text))p.text(L"Conteúdo do texto",{rx+12,364,right-24,22},MUTED);else{p.text(selected&&layer(selected)?layerName(*layer(selected)):L"Nenhuma camada selecionada",{rx+12,371,right-24,27});p.text(L"P  Posição    S  Escala    R  Rotação\nT  Opacidade    K  Inserir keyframe",{rx+12,402,right-24,52},MUTED,DT_LEFT|DT_WORDBREAK);}}
 if(inspectorFx||editing){const float fxX=editing?12:rx+12,fxY=editing?120:148,fxW=editing?left-24:right-24;p.text(L"Efeito aplicado",{fxX,fxY-21,fxW,18},MUTED);p.text(L"Parâmetro",{fxX,fxY+44,fxW,20},MUTED);p.text(L"Valor",{fxX,fxY+109,fxW,20},MUTED);if(applied.empty())p.text(L"Escolha um efeito no catálogo\ne clique em Aplicar efeito.",{fxX,fxY+215,fxW,54},MUTED,DT_LEFT|DT_WORDBREAK);}
 heading(graph?L"Editor de gráfico: "+title:L"Timeline: "+title,rowX,bottom,width-rowX);
 SelectObject(dc,monoFont);p.text(timecode(status.playhead.value,status.compFps),{width-185,bottom+2,170,28},ACCENT,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,font);
 const float rulerY=bottom+75,rowsY=bottom+105,endY=height-43,rh=rowHeight();
 p.fill({rowX,rulerY,origin-rowX,30},FIELD);p.text(editing?L"   VIS   TRACK / CLIP":L"  VIS    #    NOME DA CAMADA",{rowX+8,rulerY,origin-rowX-12,28},MUTED);p.fill({origin,rulerY,width-origin,30},FIELD);
 const int temporalClip=SaveDC(dc);IntersectClipRect(dc,px(origin),px(rulerY),px(width),px(endY));
 // Ruler, clips, keys and playhead share the same time transform at every zoom.
 const double fps=std::max(1.,status.compFps),desired=92/geometry.pixelsPerFrame;const double steps[]={1,2,5,10,15,30,60,150,300,600,1500,3000};double interval=steps[11];for(double step:steps)if(step>=desired){interval=step;break;}
 const i64 first=i64(std::floor(geometry.scrollFrames/interval));
 for(i64 tick=first;tick<first+1000;++tick){const double frame=tick*interval;const float x=float(geometry.x(frame));if(x>width)break;if(x<origin)continue;p.line(x,rulerY+18,x,endY,LINE);p.text(timecode(i64(frame),fps),{x+4,rulerY,91,19},MUTED);for(int j=1;j<5;++j){const float xx=float(geometry.x(frame+interval*j/5));if(xx<width)p.line(xx,rulerY+25,xx,rulerY+30,LINE);}}
 RestoreDC(dc,temporalClip);
 const int visible=std::max(0,int((endY-rowsY)/rh));
 for(int v=0;v<visible&&v+rowScroll<int(rows.size());++v){const int ri=v+rowScroll;const auto& row=rows[ri];const auto& l=layers[row.index];const float y=rowsY+v*rh;const bool isSelected=l.id==selected;
  p.fill({rowX,y,origin-rowX,rh-1},isSelected?SELECTED:(ri%2?FIELD:PANEL));p.line(rowX,y+rh-1,width,y+rh-1,LINE);
  if(row.property<0){p.eye(rowX+23,y+rh/2,l.flags&bridge::kLayerRowFlagVisible);const auto tag=editing?(l.kind==u32(LayerKind::Audio)?L"A":L"V"):L"";p.text(std::wstring(tag)+std::to_wstring(row.index+1),{rowX+41,y,32,rh-1},MUTED);p.text(row.label,{rowX+77,y,origin-rowX-85,rh-1});hits.push_back({{rowX,y,origin-rowX,rh-1},1000,l.id,{},ri});hits.push_back({{rowX+8,y,28,rh-1},1001,l.id,{},ri});}
  else{p.text(L"◇  "+row.label,{rowX+60,y,origin-rowX-68,rh-1},MUTED);hits.push_back({{rowX,y,origin-rowX,rh-1},1005,l.id,{},ri});}
  const int clip=SaveDC(dc);IntersectClipRect(dc,px(origin),px(rowsY),px(width),px(endY));
  if(row.property<0){const float a=float(geometry.x(l.startFrame)),b=float(geometry.x(l.endFrame)),start=std::max(origin,a),end=std::min(width,b),clipH=rh-9;
   if(end>start){const COLORREF color=l.kind==u32(LayerKind::Audio)?RGB(45,96,82):l.kind==u32(LayerKind::Text)?RGB(94,73,122):l.flags&bridge::kLayerRowFlagThreeD?RGB(118,86,47):RGB(56,90,126);p.fill({start,y+4,end-start,clipH},color);p.line(start,y+5,end,y+5,isSelected?ACCENT:RGB(107,134,162));if(isSelected)p.outline({start,y+4,end-start,clipH},ACCENT);if(end-start>30)p.text(row.label,{start+8,y+5,end-start-16,editing&&l.kind==u32(LayerKind::Audio)?16.f:clipH-2});
    if(editing&&l.kind==u32(LayerKind::Audio)){std::array<u8,128> waveform{};const u32 count=std::clamp(u32((end-start)/3),1u,u32(waveform.size()));const double firstFrame=geometry.scrollFrames+(start-origin)/geometry.pixelsPerFrame,framesPerBucket=(end-start)/count/geometry.pixelsPerFrame;const u32 n=std::min(count,engine.query_waveform(l.id,firstFrame,framesPerBucket,count,waveform.data()));for(u32 i=0;i<n;++i){const float xx=start+(i+.5f)*(end-start)/count,amplitude=waveform[i]*4.f/255,cy=y+clipH-3;p.line(xx,cy-amplitude,xx,cy+amplitude,RGB(194,222,210));}}
    hits.push_back({{start,y+3,end-start,clipH+2},1002,l.id,{},ri});if(a>=origin&&a<width){p.line(a+2,y+8,a+2,y+clipH,FG);hits.push_back({{std::max(origin,a-4),y,8,rh-1},1003,l.id,{},ri});}if(b>origin&&b<=width){p.line(b-3,y+8,b-3,y+clipH,FG);hits.push_back({{b-4,y,8,rh-1},1004,l.id,{},ri});}}
  }
  if(row.property>=0||!expanded||!isSelected)for(int pass=0;pass<3;++pass)for(const auto& k:keyframes[l.id]){
   if(row.property>=0&&(int(k.property)!=row.property||k.effectIndex!=row.effect||k.paramIndex!=row.param))continue;
   const i64 frame=i64(k.time)+l.startFrame-l.offsetFrames;const bool moving=drag==8&&dragHit.layer==l.id&&dragHit.key.property==k.property&&dragHit.key.effectIndex==k.effectIndex&&dragHit.key.paramIndex==k.paramIndex&&dragHit.key.time==k.time;
   const int drawPass=moving?2:isSelected&&frame==status.playhead.value?1:0;if(pass!=drawPass)continue;
   const float x=float(geometry.key_x(k.time,l.startFrame,l.offsetFrames));if(x<origin||x>=width||frame<l.startFrame||frame>l.endFrame)continue;p.diamond(x,y+rh/2,isSelected?ACCENT:FG);hits.push_back({{std::max(origin,x-7),y+rh/2-9,std::min(width,x+7)-std::max(origin,x-7),18},1006,l.id,k,ri});
  }
  RestoreDC(dc,clip);
 }
 if(layers.empty()){p.text(L"Importe sua mídia para começar",{origin+25,rowsY+14,width-origin-50,28});p.text(L"Ctrl + I  ·  Ou arraste um arquivo para esta janela",{origin+25,rowsY+45,width-origin-50,28},MUTED);}
 const int overlayClip=SaveDC(dc);IntersectClipRect(dc,px(origin),px(rulerY),px(width),px(endY));
 std::array<i64,3072> marks{};const auto markCount=std::min(1024u,engine.query_markers(marks.data(),1024));for(u32 i=0;i<markCount;++i){const float x=float(geometry.x(double(marks[i*3])));if(x>=origin&&x<width){p.diamond(x,rulerY+7,RGB(236,181,94));hits.push_back({{std::max(origin,x-7),rulerY,14,15},1007,0,{},int(marks[i*3])});}}
 if(graph&&selected){p.fill({origin,rowsY,width-origin,endY-rowsY},FIELD);std::array<float,256> samples{};const auto* l=layer(selected);const i64 offset=l?l->offsetFrames-l->startFrame:0;const i32 from=i32(geometry.frame(origin)+offset),to=i32(geometry.frame(width)+offset);engine.query_track_curve(selected,u32(activeProperty),activeEffect,activeParam,from,to,samples.data(),u32(samples.size()));auto bounds=std::minmax_element(samples.begin(),samples.end());const float range=std::max(.01f,*bounds.second-*bounds.first);for(int j=0;j<5;++j){const float yy=rowsY+28+j*(endY-rowsY-44)/4;p.line(origin,yy,width,yy,LINE);p.text(number(*bounds.second-j*range/4,2),{origin+8,yy-18,85,18},MUTED);}for(size_t i=1;i<samples.size();++i){const float x=origin+float(i-1)/(samples.size()-1)*(width-origin),x2=origin+float(i)/(samples.size()-1)*(width-origin);const float y=endY-16-(samples[i-1]-*bounds.first)/range*(endY-rowsY-44),y2=endY-16-(samples[i]-*bounds.first)/range*(endY-rowsY-44);p.line(x,y,x2,y2,ACCENT);}std::wstring label=L"Propriedade selecionada";for(int i=0;i<10;++i)if(activeProperty==Props[i])label=PropNames[i];p.text(label,{origin+12,rowsY+2,320,23},FG);hits.erase(std::remove_if(hits.begin(),hits.end(),[](const Hit& h){return h.action>=1000&&h.action<=1006;}),hits.end());}
 const float playX=float(geometry.x(double(status.playhead.value)));if(playX>=origin&&playX<width){p.line(playX,rulerY,playX,endY,ACCENT);p.arrow(playX,rulerY,ACCENT);}
 RestoreDC(dc,overlayClip);p.line(origin,rulerY,origin,endY,LINE);
 p.fill({0,height-24,width,24},FIELD);p.text(notice,{12,height-24,width-225,24},busy?ACCENT:MUTED);p.text(L"AUREA PC 0.0.1  |  BETA TESTE",{width-219,height-24,207,24},MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
}
void App::pointerDown(float x,float y){
 SetFocus(window);if(busy)return;const float bottom=height-timelineHeight;
 down={LONG(x),LONG(y)};
 if(std::abs(y-bottom)<5){drag=1;SetCapture(window);return;}
 if(y<bottom&&std::abs(x-left)<5){drag=2;SetCapture(window);return;}
 if(y<bottom&&std::abs(x-(width-right))<5){drag=3;SetCapture(window);return;}
 for(auto i=hits.rbegin();i!=hits.rend();++i)if(i->rect.contains(x,y)){
  const auto h=*i;
  if(h.action==1007){seekFrame(h.row);return;}
  if(h.layer!=selected)select(h.layer);
  if(h.action==1001){Command c;c.type=CommandType::LayerSetVisible;c.layer_visible={layerId(),!(layer(selected)->flags&bridge::kLayerRowFlagVisible)};send(c);return;}
  if(h.action==1005){activeProperty=rows[h.row].property;activeEffect=rows[h.row].effect;activeParam=rows[h.row].param;return;}
  if(h.action==1000)return;
  auto* l=layer(h.layer);if(!l||l->flags&bridge::kLayerRowFlagLocked)return;
  if(h.action==1006){activeProperty=int(h.key.property);activeEffect=h.key.effectIndex;activeParam=h.key.paramIndex;}
  dragHit=h;dragStart=l->startFrame;dragEnd=l->endFrame;dragOffset=l->offsetFrames;dragFrame=geometry.frame(x);drag=h.action==1006?8:h.action==1003?9:h.action==1004?10:6;SetCapture(window);return;
 }
 if(x>=geometry.origin&&y>=bottom+75&&y<height-40){drag=4;command(CommandType::PlaybackScrubBegin);Command c;c.type=CommandType::PlaybackScrub;c.seek.time=tick_at(FrameIndex{std::clamp<i64>(geometry.frame(x),0,std::max<i64>(0,status.duration.value-1))},status.compFps);send(c);SetCapture(window);}
}
void App::pointerMove(float x,float y){
 if(drag==1){timelineHeight=height-y;layout();return;}if(drag==2){left=x;layout();return;}if(drag==3){right=width-x;layout();return;}
 if(drag==5){geometry.scrollFrames=std::max(0.,dragScroll+(down.x-x)/geometry.pixelsPerFrame);refresh();return;}
 if(drag==4){Command c;c.type=CommandType::PlaybackScrub;c.seek.time=tick_at(FrameIndex{std::clamp<i64>(geometry.frame(x),0,std::max<i64>(0,status.duration.value-1))},status.compFps);send(c);return;}
 if(drag==6||drag==8||drag==9||drag==10){if(std::abs(x-down.x)<2&&!draggingCommands)return;beginGroup();const i64 delta=geometry.frame(x)-dragFrame;Command c;
  if(drag==8){const i64 time=std::clamp<i64>(i64(dragHit.key.time)+delta,dragOffset,dragOffset+dragEnd-dragStart);if(time==dragHit.key.time)return;c.type=CommandType::KeyframeMove;c.keyframe_move={TrackRef{layerId(),static_cast<TrackProperty>(dragHit.key.property),dragHit.key.effectIndex,dragHit.key.paramIndex},FrameIndex{dragHit.key.time},FrameIndex{time}};send(c);dragHit.key.time=i32(time);dragFrame=geometry.frame(x);}
  else{c.type=CommandType::LayerSetTimeRange;i64 start=dragStart,end=dragEnd,offset=dragOffset;if(drag==6){const i64 move=std::max(-start,delta);start+=move;end+=move;}else if(drag==9){start=std::clamp<i64>(start+delta,0,end-1);offset+=start-dragStart;}else end=std::clamp<i64>(end+delta,start+1,status.duration.value);c.layer_range={layerId(),FrameIndex{start},FrameIndex{end},FrameIndex{offset},1};send(c);}
 }
}
void App::pointerUp(){if(drag==4)command(CommandType::PlaybackScrubEnd);endGroup();drag=0;ReleaseCapture();refresh();}
void App::context(float x,float y){
 Hit target{};for(auto it=hits.rbegin();it!=hits.rend();++it)if(it->rect.contains(x,y)){target=*it;break;}if(target.layer&&target.layer!=selected)select(target.layer);
 HMENU m=CreatePopupMenu();if(target.action==1006){AppendMenuW(m,MF_STRING,1201,L"Excluir keyframe");AppendMenuW(m,MF_STRING,1202,L"Linear");AppendMenuW(m,MF_STRING,1203,L"Suavizar entrada e saída");AppendMenuW(m,MF_STRING,1204,L"Bounce");AppendMenuW(m,MF_STRING,1205,L"Hold");}else{AppendMenuW(m,MF_STRING,Duplicate,L"Duplicar\tCtrl+D");AppendMenuW(m,MF_STRING,Split,L"Dividir no playhead\tCtrl+Shift+D");AppendMenuW(m,MF_STRING,Raise,L"Mover camada para cima");AppendMenuW(m,MF_STRING,Lower,L"Mover camada para baixo");AppendMenuW(m,MF_STRING,Delete,L"Excluir\tDel");}
 POINT at{px(x),px(y)};ClientToScreen(window,&at);const int choice=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,at.x,at.y,0,window,nullptr);DestroyMenu(m);
 if(choice>=1201){Command c;const TrackRef track{LayerId::unpack(target.layer),static_cast<TrackProperty>(target.key.property),target.key.effectIndex,target.key.paramIndex};if(choice==1201){c.type=CommandType::KeyframeDelete;c.keyframe={track,FrameIndex{target.key.time},0};}else{c.type=CommandType::KeyframeSetEasing;c.keyframe_interp.track=track;c.keyframe_interp.time=FrameIndex{target.key.time};c.keyframe_interp.interp=choice==1202?Interpolation::Linear:choice==1203?Interpolation::EaseInOut:choice==1204?Interpolation::Bounce:Interpolation::Hold;c.keyframe_interp.power=2;c.keyframe_interp.bx1=.42f;c.keyframe_interp.by1=0;c.keyframe_interp.bx2=.58f;c.keyframe_interp.by2=1;}send(c);}else if(choice)action(choice);
}
// Native controls remain keyboard accessible; shortcut handling never intercepts text entry.
void App::key(WPARAM code){const bool ctrl=GetKeyState(VK_CONTROL)<0,shift=GetKeyState(VK_SHIFT)<0;
 if(ctrl){switch(code){case 'N':action(New);return;case 'O':action(Open);return;case 'S':action(shift?SaveAs:Save);return;case 'I':action(Import);return;case 'M':action(Export);return;case 'Z':action(shift?Redo:Undo);return;case 'Y':action(Redo);return;case 'D':action(shift?Split:Duplicate);return;}}
 if(code==VK_SPACE){action(Play);return;}if(code==VK_DELETE){action(Delete);return;}if(code==VK_F1){action(Help);return;}if(code==VK_HOME){seekFrame(0);return;}if(code==VK_END){seekFrame(status.duration.value-1);return;}
 if(code==VK_LEFT||code==VK_RIGHT){if(ctrl)action(code==VK_LEFT?Prev:Next);else seekFrame(status.playhead.value+(code==VK_LEFT?-1:1)*(shift?10:1));return;}
 if(ctrl&&code==VK_F2){action(WorkspaceCompose);return;}if(ctrl&&code==VK_F3){action(WorkspaceEdit);return;}
 if(code=='M'){action(Marker);return;}if(code=='K'){addKey(activeProperty);return;}if(code=='U'){action(Expand);return;}if(code=='P'||code=='S'||code=='R'||code=='T'){const int index=code=='P'?0:code=='S'?3:code=='R'?8:9;activeProperty=Props[index];inspectorFx=false;refresh();SetFocus(fields[index]);SendMessageW(fields[index],EM_SETSEL,0,-1);return;}
 if(code==VK_OEM_PLUS||code==VK_ADD)action(ZoomIn);if(code==VK_OEM_MINUS||code==VK_SUBTRACT)action(ZoomOut);
}
void App::action(int id){
 if(id==Help){MessageBoxW(window,L"AUREA PC 0.0.1 · Beta teste\n\nCtrl+N  Novo projeto\nCtrl+O  Abrir .aurea\nCtrl+S  Salvar  •  Ctrl+Shift+S  Salvar como\nCtrl+I  Importar mídia  •  Ctrl+M  Exportar MP4\nEspaço  Reproduzir / pausar\nSetas  Avançar / recuar 1 frame  •  Shift: 10 frames\nCtrl+setas  Ir à próxima marca / keyframe\nCtrl+D  Duplicar  •  Ctrl+Shift+D  Dividir\nCtrl+Z / Ctrl+Y  Desfazer / refazer\nP / S / R / T  Posição / escala / rotação / opacidade\nK  Inserir keyframe  •  M  Adicionar marca\nU  Expandir keyframes  •  Del  Excluir camada\nCtrl+roda  Zoom na timeline\nShift+roda ou botão do meio  Navegar no tempo\n\nArraste as divisórias para ajustar os painéis.\nArraste a camada selecionada na composição para movê-la.\nClique direito em um keyframe para escolher a interpolação.\n\nRequer Windows 10/11 x64 e GPU com Vulkan.\nWorkspaces: Ctrl+F2 Composição / Ctrl+F3 Edição.",L"Atalhos e informações",MB_OK);return;}
 if(id==Quit){SendMessageW(window,WM_CLOSE,0,0);return;}if(busy)return;
 if(id==CancelExport){(void)engine.cancel_export();return;}if(engine.export_progress().running){message(L"Exportação em andamento. Use Arquivo > Cancelar exportação para interromper.");return;}
 switch(id){
 case New:if(mayDiscard()){error(engine.new_project(1920,1080,30,"Sem titulo"));projectPath.clear();title=L"Sem título";selected=0;geometry.scrollFrames=0;revision=~0u;}break;
 case Open:{auto p=choose(false,L"Projeto AUREA (*.aurea)\0*.aurea\0\0",L"aurea");if(!p.empty())importFile(p);break;}
 case Save:saveProject();break;case SaveAs:saveProject(true);break;
 case Import:{auto p=choose(false,L"Mídia e projetos\0*.mp4;*.mov;*.m4v;*.avi;*.wmv;*.mp3;*.wav;*.m4a;*.aac;*.flac;*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.svg;*.glb;*.gltf;*.fbx;*.obj;*.aurea\0Todos os arquivos\0*.*\0\0",nullptr);if(!p.empty())importFile(p);break;}
 case Text:case Rectangle:case Ellipse:case Cube:{auto result=id==Text?engine.add_text("Texto"):id==Cube?engine.add_shape3d(0,"Cubo"):engine.add_shape(id==Ellipse?1:0);if(result.ok())select(*result);else error(result.status());break;}
 case Undo:command(CommandType::Undo);break;case Redo:command(CommandType::Redo);break;
 case Play:command(CommandType::PlaybackToggle);break;
 case Duplicate:case Delete:if(selected){Command c;c.type=id==Duplicate?CommandType::LayerDuplicate:CommandType::LayerDelete;c.layer_ref.layer=layerId();send(c);}break;
 case Split:if(selected){Command c;c.type=CommandType::LayerSplit;c.layer_split={layerId(),status.playhead,{}};send(c);}break;
 case Raise:case Lower:if(selected&&layer(selected)){Command c;c.type=CommandType::LayerReorder;c.layer_reorder={layerId(),u32(std::clamp(int(layer(selected)->zIndex)+(id==Raise?-1:1),0,int(layers.size())-1))};send(c);}break;
 case Marker:engine.toggle_marker(status.playhead.value);revision=~0u;break;
 case Prev:case Next:{std::vector<i64> times{0,status.duration.value-1};std::array<i64,3072> m{};const u32 n=std::min(1024u,engine.query_markers(m.data(),1024));for(u32 i=0;i<n;++i)times.push_back(m[i*3]);Engine::KeyframeNavigation nav{};if(selected&&engine.query_keyframe_navigation(selected,u32(activeProperty),activeEffect,activeParam,status.playhead.value,nav)){if(nav.flags&2)times.push_back(nav.previousFrame);if(nav.flags&4)times.push_back(nav.nextFrame);}std::sort(times.begin(),times.end());i64 target=id==Prev?0:status.duration.value-1;for(i64 t:times)if(id==Prev&&t<status.playhead.value)target=t;else if(id==Next&&t>status.playhead.value){target=t;break;}seekFrame(target);break;}
 case ZoomIn:case ZoomOut:geometry.zoom_at(geometry.x(double(status.playhead.value)),std::clamp(geometry.pixelsPerFrame*(id==ZoomIn?1.3:1/1.3),.1,50.));geometry.scrollFrames=std::max(0.,geometry.scrollFrames);break;
 case Reset:left=editing?245.f:260.f;right=300;timelineHeight=editing?360.f:330.f;layout();break;
 case WorkspaceCompose:case WorkspaceEdit:if(editing!=(id==WorkspaceEdit)){editing=id==WorkspaceEdit;inspectorFx=false;graph=false;expanded=!editing;rowScroll=0;left=editing?245.f:260.f;right=300;timelineHeight=editing?360.f:330.f;layout();}break;
 case InspectorTransform:inspectorFx=false;break;case InspectorEffects:inspectorFx=!editing;break;
 case Expand:expanded=!expanded;revision=~0u;break;case Graph:graph=!graph;break;
 case AutoKey:autoKey=!autoKey;SetWindowTextW(buttons[AutoKey],autoKey?L"Auto-key: ON":L"Auto-key: OFF");break;
 case LinkScale:linked=!linked;SetWindowTextW(buttons[LinkScale],linked?L"Escala vinculada: ON":L"Escala vinculada: OFF");break;
 case Export:exportVideo();break;
 case Png:{const auto path=choose(true,L"Imagem PNG\0*.png\0\0",L"png",title+L".png");if(!path.empty())run([this,path]{std::vector<u8> pixels;u32 w=0,h=0;taskStatus=engine.capture_frame_rgba(std::max(status.compWidth,status.compHeight),pixels,w,h);if(taskStatus.ok()&&!save_png(path,pixels.data(),w,h))taskStatus=Status{Errc::IoError};taskNotice=L"Quadro PNG salvo.";});break;}
 case AddEffect:if(selected){const auto idx=SendMessageW(catalog,LB_GETCURSEL,0,0);if(idx>=0&&size_t(idx)<filtered.size()){Command c;c.type=CommandType::EffectAdd;c.effect_add={layerId(),filtered[idx].type,UINT32_MAX};send(c);inspectorFx=!editing;}}else message(L"Selecione uma camada antes de aplicar o efeito.");break;
 case RemoveEffect:case ToggleEffect:if(selected&&selectedEffect!=kInvalidIndex){Command c;if(id==RemoveEffect){c.type=CommandType::EffectRemove;c.effect_ref={layerId(),EffectId{selectedEffect}};}else {const auto index=SendMessageW(effects,CB_GETCURSEL,0,0);if(index<0||size_t(index)>=applied.size())break;c.type=CommandType::EffectSetEnabled;c.effect_enabled={layerId(),EffectId{selectedEffect},!applied[index].enabled};}send(c);}break;
 case 406:if(selected&&!params.empty()){const auto i=SendMessageW(paramList,CB_GETCURSEL,0,0);if(i>=0&&size_t(i)<params.size()){activeProperty=int(TrackProperty::EffectParam);activeEffect=selectedEffect;activeParam=params[i].index*4;Command c;c.type=CommandType::KeyframeInsert;c.keyframe={TrackRef{layerId(),TrackProperty::EffectParam,selectedEffect,params[i].index*4},FrameIndex{detail.localPlayhead},params[i].value[0]};send(c);}}break;
 case TextApply:if(selected){Command c;c.type=CommandType::TextSetContent;c.layer_ref.layer=layerId();send(c,utf8(window_text(textBox)));}break;
 case Color:if(selected){static COLORREF custom[16]{};CHOOSECOLORW cc{sizeof(cc)};cc.hwndOwner=window;cc.rgbResult=RGB(101,218,183);cc.lpCustColors=custom;cc.Flags=CC_FULLOPEN|CC_RGBINIT;if(ChooseColorW(&cc)){Command c;c.type=detail.kind==u32(LayerKind::Text)?CommandType::TextSetColor:CommandType::ShapeSetFill;c.text_color={layerId(),GetRValue(cc.rgbResult)/255.f,GetGValue(cc.rgbResult)/255.f,GetBValue(cc.rgbResult)/255.f,1};send(c);}}break;
 case Settings:message(L"Composição: "+std::to_wstring(status.compWidth)+L" × "+std::to_wstring(status.compHeight)+L" • "+number(status.compFps)+L" fps");break;
 }refresh();for(auto [buttonId,w]:buttons)InvalidateRect(w,nullptr,FALSE);
}
void App::timer(){if(!ready||busy)return;refresh();auto p=engine.export_progress();if(p.running){exportWasRunning=true;notice=L"Exportando MP4: "+std::to_wstring(p.framesDone)+L" / "+std::to_wstring(p.framesTotal)+L" quadros";}else if(exportWasRunning){exportWasRunning=false;notice=p.result==Errc::Ok?L"MP4 exportado com sucesso.":L"Exportação: "+wide(std::string(to_string(p.result)));}if(status.playing){const auto x=geometry.x(double(status.playhead.value));if(x>width-30){geometry.scrollFrames=std::max(0.,double(status.playhead.value)-(width-geometry.origin)*.2/geometry.pixelsPerFrame);}}}
HMENU menu(){auto root=CreateMenu();auto add=[&](const wchar_t* name,std::initializer_list<std::pair<int,const wchar_t*>> items){auto m=CreatePopupMenu();for(auto [id,text]:items)AppendMenuW(m,id?MF_STRING:MF_SEPARATOR,id,text);AppendMenuW(root,MF_POPUP,reinterpret_cast<UINT_PTR>(m),name);};
 add(L"Arquivo",{{New,L"Novo projeto\tCtrl+N"},{Open,L"Abrir projeto…\tCtrl+O"},{Save,L"Salvar\tCtrl+S"},{SaveAs,L"Salvar como…\tCtrl+Shift+S"},{0,L""},{Import,L"Importar mídia…\tCtrl+I"},{Export,L"Exportar MP4…\tCtrl+M"},{Png,L"Salvar quadro PNG…"},{CancelExport,L"Cancelar exportação"},{0,L""},{Quit,L"Sair"}});
 add(L"Editar",{{Undo,L"Desfazer\tCtrl+Z"},{Redo,L"Refazer\tCtrl+Y"},{Duplicate,L"Duplicar camada\tCtrl+D"},{Split,L"Dividir camada\tCtrl+Shift+D"},{Delete,L"Excluir camada\tDel"}});
 add(L"Camada",{{Text,L"Texto"},{Rectangle,L"Retângulo"},{Ellipse,L"Elipse"},{Cube,L"Cubo 3D"},{Import,L"Importar modelo / mídia…"},{Color,L"Cor…"},{Raise,L"Mover para cima"},{Lower,L"Mover para baixo"}});
 add(L"Animação",{{Play,L"Reproduzir / pausar\tEspaço"},{Marker,L"Adicionar marca\tM"},{Prev,L"Marca / keyframe anterior\tCtrl+←"},{Next,L"Próxima marca / keyframe\tCtrl+→"},{AutoKey,L"Alternar auto-key"},{Expand,L"Mostrar keyframes\tU"},{Graph,L"Mostrar curva"}});
 add(L"Janela",{{WorkspaceCompose,L"Workspace Composição\tCtrl+F2"},{WorkspaceEdit,L"Workspace Edição\tCtrl+F3"},{0,L""},{InspectorTransform,L"Propriedades de transformação"},{InspectorEffects,L"Controles de efeito"},{0,L""},{ZoomIn,L"Ampliar timeline\t+"},{ZoomOut,L"Reduzir timeline\t−"},{Reset,L"Restaurar painéis"}});add(L"Ajuda",{{Help,L"Atalhos e informações\tF1"}});return root;
}
LRESULT CALLBACK WindowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 if(!app)return DefWindowProcW(w,msg,wp,lp);auto& a=*app;
 switch(msg){
 case WM_CREATE:a.window=w;a.makeControls();a.layout();DragAcceptFiles(w,TRUE);SetTimer(w,1,33,nullptr);return 0;
 case WM_SIZE:if(a.preview&&!a.busy)a.layout();return 0;
 case WM_DPICHANGED:{a.dpi=HIWORD(wp)/96.f;a.updateFonts();const auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);a.layout();return 0;}
 case WM_GETMINMAXINFO:{auto* m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={a.px(1080),a.px(760)};return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(memory,bitmap);a.paint(memory);BitBlt(dc,0,0,r.right,r.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);EndPaint(w,&ps);return 0;}
 case WM_CTLCOLORSTATIC:{auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,MUTED);SetBkColor(dc,PANEL);return reinterpret_cast<LRESULT>(a.panelBrush);}
 case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,FG);SetBkColor(dc,FIELD);return reinterpret_cast<LRESULT>(a.fieldBrush);}
 case WM_DRAWITEM:{auto* d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);const int id=int(d->CtlID);const bool active=(id==WorkspaceCompose&&!a.editing)||(id==WorkspaceEdit&&a.editing)||(id==InspectorTransform&&!a.inspectorFx)||(id==InspectorEffects&&a.inspectorFx)||(id==Graph&&a.graph)||(id==Expand&&a.expanded)||(id==AutoKey&&a.autoKey);const bool disabled=d->itemState&ODS_DISABLED;
  FillRect(d->hDC,&d->rcItem,a.panelBrush);RECT r=d->rcItem;InflateRect(&r,-1,-1);auto brush=CreateSolidBrush(d->itemState&ODS_SELECTED?SELECTED:active?SELECTED:id==Export?RGB(39,75,110):FIELD);FillRect(d->hDC,&r,brush);DeleteObject(brush);Painter p(d->hDC,a.dpi,a.font);const float cx=(r.left+r.right)/(2*a.dpi),cy=(r.top+r.bottom)/(2*a.dpi);const COLORREF ink=disabled?MUTED:active?ACCENT:FG;
  if(id>=600&&id<610||id==406){const bool here=id>=600&&id<610&&(a.detail.keyAtPlayheadMask&(1u<<Props[id-600]));p.diamond(cx,cy,disabled?MUTED:here?ACCENT:FG,here);}
  else if(id==Play){if(a.status.playing){p.fill({cx-5,cy-5,3,10},ink);p.fill({cx+2,cy-5,3,10},ink);}else{POINT pts[]={{LONG((cx-4)*a.dpi),LONG((cy-6)*a.dpi)},{LONG((cx+6)*a.dpi),LONG(cy*a.dpi)},{LONG((cx-4)*a.dpi),LONG((cy+6)*a.dpi)}};auto b=CreateSolidBrush(ink);auto ob=SelectObject(d->hDC,b);auto op=SelectObject(d->hDC,GetStockObject(NULL_PEN));Polygon(d->hDC,pts,3);SelectObject(d->hDC,ob);SelectObject(d->hDC,op);DeleteObject(b);}}
  else if(id==Prev||id==Next){const float sign=id==Prev?-1.f:1.f;p.line(cx-sign*4,cy-5,cx+sign*3,cy,ink);p.line(cx+sign*3,cy,cx-sign*4,cy+5,ink);p.line(cx+sign*6,cy-6,cx+sign*6,cy+6,ink);}
  else if(id==ZoomIn||id==ZoomOut){p.line(cx-5,cy,cx+5,cy,ink);if(id==ZoomIn)p.line(cx,cy-5,cx,cy+5,ink);}
  else {SetTextColor(d->hDC,ink);auto text=window_text(d->hwndItem);DrawTextW(d->hDC,text.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}
  if(active)p.line(r.left/a.dpi,r.bottom/a.dpi-1,r.right/a.dpi,r.bottom/a.dpi-1,ACCENT);if(d->itemState&ODS_FOCUS){InflateRect(&r,-2,-2);DrawFocusRect(d->hDC,&r);}return TRUE;}
 case WM_NOTIFY:{auto* header=reinterpret_cast<NMHDR*>(lp);if(header&&header->code==TTN_GETDISPINFOW){auto* info=reinterpret_cast<NMTTDISPINFOW*>(lp);static std::wstring tip;tip=window_text(reinterpret_cast<HWND>(info->hdr.idFrom));info->lpszText=tip.data();return 0;}break;}
 case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT&&reinterpret_cast<HWND>(wp)==w){POINT pt;GetCursorPos(&pt);ScreenToClient(w,&pt);const float x=pt.x/a.dpi,y=pt.y/a.dpi,bottom=a.height-a.timelineHeight;const wchar_t* cursor=std::abs(y-bottom)<5?IDC_SIZENS:y>43&&y<bottom&&(std::abs(x-a.left)<5||std::abs(x-(a.width-a.right))<5)?IDC_SIZEWE:IDC_ARROW;SetCursor(LoadCursorW(nullptr,cursor));return TRUE;}break;
 case WM_COMMAND:{const int id=LOWORD(wp),event=HIWORD(wp);if(a.updating)return 0;
  if(id==400&&event==EN_CHANGE){a.filterEffects();return 0;}if(id==401&&event==LBN_DBLCLK){a.action(AddEffect);return 0;}
  if(id==402&&event==LBN_SELCHANGE){auto index=SendMessageW(a.assets,LB_GETCURSEL,0,0);if(index>=0&&size_t(index)<a.layers.size())a.select(a.layers[index].id);return 0;}
  if(id==403&&event==CBN_SELCHANGE){auto i=SendMessageW(a.effects,CB_GETCURSEL,0,0);if(i>=0&&size_t(i)<a.applied.size()){a.selectedEffect=a.applied[i].effectId;a.updateParams();}return 0;}
  if(id==404&&event==CBN_SELCHANGE){auto i=SendMessageW(a.paramList,CB_GETCURSEL,0,0);if(i>=0&&size_t(i)<a.params.size())SetWindowTextW(a.paramValue,number(a.params[i].value[0],3).c_str());return 0;}
  if(id==405&&event==EN_KILLFOCUS){double value;const auto i=SendMessageW(a.paramList,CB_GETCURSEL,0,0);if(i>=0&&size_t(i)<a.params.size()&&numeric(window_text(a.paramValue),value)){auto& param=a.params[i];const float v=float(std::clamp(value,double(param.hardMin),double(param.hardMax)));if(std::abs(v-param.value[0])>1e-6f){Command c;c.type=CommandType::EffectSetParam;c.effect_param={a.layerId(),EffectId{a.selectedEffect},param.index,v};a.send(c);}}return 0;}
  if(id>=500&&id<510){if(event==EN_SETFOCUS){a.activeProperty=Props[id-500];a.activeEffect=kInvalidIndex;a.activeParam=0;}if(event==EN_KILLFOCUS&&a.selected){double value;if(numeric(window_text(a.fields[id-500]),value)){const int p=Props[id-500];if(p>=3&&p<=5||p==12)value/=100;if(p==12)value=std::clamp(value,0.,1.);if(std::abs(value-a.propertyValue(p))>1e-5)a.setProperty(p,float(value));}else a.message(L"Digite um número válido para a propriedade.");}return 0;}
  if(id>=600&&id<610){a.addKey(Props[id-600]);return 0;}if(event==BN_CLICKED||lp==0)a.action(id);return 0;}
 case WM_KEYDOWN:a.key(wp);return 0;
 case WM_LBUTTONDOWN:a.pointerDown(GET_X_LPARAM(lp)/a.dpi,GET_Y_LPARAM(lp)/a.dpi);return 0;
 case WM_MOUSEMOVE:a.pointerMove(GET_X_LPARAM(lp)/a.dpi,GET_Y_LPARAM(lp)/a.dpi);return 0;
 case WM_LBUTTONUP:a.pointerUp();return 0;
 case WM_MBUTTONDOWN:a.drag=5;a.down={LONG(GET_X_LPARAM(lp)/a.dpi),LONG(GET_Y_LPARAM(lp)/a.dpi)};a.dragScroll=a.geometry.scrollFrames;SetCapture(w);return 0;
 case WM_MBUTTONUP:a.pointerUp();return 0;
 case WM_CONTEXTMENU:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);a.context(p.x/a.dpi,p.y/a.dpi);return 0;}
 case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);if(p.y/a.dpi<a.height-a.timelineHeight)return 0;const double delta=GET_WHEEL_DELTA_WPARAM(wp)/120.;if(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL){a.geometry.zoom_at(std::max(a.geometry.origin,p.x/a.dpi*1.),std::clamp(a.geometry.pixelsPerFrame*std::pow(1.2,delta),.1,50.));a.geometry.scrollFrames=std::max(0.,a.geometry.scrollFrames);}else if(GET_KEYSTATE_WPARAM(wp)&MK_SHIFT)a.geometry.scrollFrames=std::max(0.,a.geometry.scrollFrames-delta*50/a.geometry.pixelsPerFrame);else a.rowScroll=std::max(0,a.rowScroll-int(delta*3));a.refresh();return 0;}
 case WM_HSCROLL:{if(reinterpret_cast<HWND>(lp)==a.scrollbar){SCROLLINFO s{sizeof(s),SIF_ALL};GetScrollInfo(a.scrollbar,SB_CTL,&s);switch(LOWORD(wp)){case SB_THUMBTRACK:s.nPos=s.nTrackPos;break;case SB_LINELEFT:s.nPos-=10;break;case SB_LINERIGHT:s.nPos+=10;break;case SB_PAGELEFT:s.nPos-=int(s.nPage);break;case SB_PAGERIGHT:s.nPos+=int(s.nPage);break;}a.geometry.scrollFrames=std::max(0,s.nPos);a.refresh();}return 0;}
 case WM_DROPFILES:{auto drop=reinterpret_cast<HDROP>(wp);wchar_t path[32768];if(DragQueryFileW(drop,0,path,32768))a.importFile(path);DragFinish(drop);return 0;}
 case WM_TIMER:a.timer();return 0;
 case WM_TASK_DONE:a.finishTask();return 0;
 case WM_CLOSE:if(a.mayDiscard()){KillTimer(w,1);a.engine.stop_render_thread();a.engine.detach_surface();DestroyWindow(w);}return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 }return DefWindowProcW(w,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 const bool headless=argv&&argc>=2&&std::wstring(argv[1])==L"--self-test";
 if(headless&&argc<3){std::fputs("Usage: AUREA-PC.exe --self-test <output-directory>\n",stderr);LocalFree(argv);return 2;}
 if(!initialize_media()){
  if(headless){std::fputs("AUREA PC: Windows Media Foundation initialization failed.\n",stderr);std::error_code ec;fs::create_directories(argv[2],ec);std::ofstream log(fs::path(argv[2])/L"startup-error.txt");log<<"Windows Media Foundation initialization failed. Self-test did not run.\n";}
  else MessageBoxW(nullptr,L"O Windows não conseguiu iniciar os serviços de mídia. Reinicie o aplicativo e confira se os componentes de mídia do Windows estão instalados.",L"AUREA PC 0.0.1",MB_ICONERROR|MB_OK);
  LocalFree(argv);return 1;
 }
 if(headless){const int result=run_desktop_tests(argv[2]);LocalFree(argv);shutdown_media();return result;}
 INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&cc);
 int exitCode=0;{
 App value;app=&value;PWSTR data=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&data))){value.appDirectory=fs::path(data)/L"AUREA"/L"Desktop";CoTaskMemFree(data);}else value.appDirectory=fs::temp_directory_path()/L"AureaDesktop";
 std::error_code ec;fs::create_directories(value.appDirectory/L"cache",ec);
 WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=instance;wc.lpszClassName=L"AureaDesktop";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassW(&wc);
 const auto window=CreateWindowExW(0,wc.lpszClassName,L"AUREA PC 0.0.1 · Beta teste",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1440,960,nullptr,menu(),instance,nullptr);
 if(!window){LocalFree(argv);return 1;}BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
 value.audio=make_audio_output();EngineConfig cfg;cfg.backend=new vk::Backend;cfg.mediaFactory=&value.media;cfg.imageLoader=load_image;cfg.exportSinkFactory=make_ffmpeg_export_sink;cfg.audioOutput=value.audio.get();cfg.documentsDirectory=utf8(value.appDirectory.wstring());cfg.cacheDirectory=utf8((value.appDirectory/L"cache").wstring());cfg.defaultFontPath="C:/Windows/Fonts/segoeui.ttf";cfg.workerCount=4;
 const auto init=value.engine.initialize(cfg);if(!init.ok()){MessageBoxW(window,(L"Não foi possível iniciar a GPU do AUREA.\nAtualize o driver de vídeo com suporte a Vulkan.\n\n"+wide(std::string(init.message()))).c_str(),L"AUREA",MB_ICONERROR);DestroyWindow(window);exitCode=1;}
 else {value.ready=true;(void)value.engine.new_project(1920,1080,30,"Sem titulo");
  std::array<bridge::EffectCatalogRow,2048> rows{};std::array<char,262144> blob{};const auto count=std::min<u32>(u32(rows.size()),value.engine.query_effect_catalog(rows.data(),u32(rows.size()),blob.data(),u32(blob.size())));for(u32 i=0;i<count;++i)value.library.push_back({rows[i].typeId,wide(std::string(blob.data()+rows[i].nameOffset,rows[i].nameLength))});std::sort(value.library.begin(),value.library.end(),[](const auto& a,const auto& b){return a.name<b.name;});value.filterEffects();
  ShowWindow(window,show);value.layout();RECT r;GetClientRect(value.preview,&r);const auto attached=value.engine.attach_surface(value.preview,r.right,r.bottom);if(!attached.ok())value.error(attached);value.engine.start_render_thread();value.refresh();
  if(argc>1)value.importFile(argv[1]);
  // File/workspace/help shortcuts stay available when a native field owns focus.
  // Undo, redo, arrows, printable characters and space remain native text editing.
  const ACCEL shortcuts[]={
   {FVIRTKEY|FCONTROL,'N',New},{FVIRTKEY|FCONTROL,'O',Open},
   {FVIRTKEY|FCONTROL|FSHIFT,'S',SaveAs},{FVIRTKEY|FCONTROL,'S',Save},
   {FVIRTKEY|FCONTROL,'I',Import},{FVIRTKEY|FCONTROL,'M',Export},
   {FVIRTKEY|FCONTROL,VK_F2,WorkspaceCompose},{FVIRTKEY|FCONTROL,VK_F3,WorkspaceEdit},
   {FVIRTKEY,VK_F1,Help}
  };
  HACCEL accelerators=CreateAcceleratorTableW(const_cast<ACCEL*>(shortcuts),int(std::size(shortcuts)));
  MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(accelerators&&TranslateAcceleratorW(window,accelerators,&msg))continue;if(msg.message==WM_KEYDOWN&&(msg.wParam==VK_TAB)){if(IsDialogMessageW(window,&msg))continue;}TranslateMessage(&msg);DispatchMessageW(&msg);}if(accelerators)DestroyAcceleratorTable(accelerators);exitCode=int(msg.wParam);
 }
 app=nullptr;
 }LocalFree(argv);shutdown_media();return exitCode;
}
