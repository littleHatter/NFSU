#include <3ds.h>
#include <sys/iosupport.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include "loader.h"
#include "runtime_s3e.h"
#include "runtime_clock.h"
#include "audio_gain.h"
#include "bottom_art.h"
#include "prompt_art.h"
#include "startup_art.h"
#include "display_toggle_art.h"

u32 __stacksize__=1024*1024;
u32 __ctru_heap_size=12*1024*1024;
u32 __ctru_linear_heap_size=4*1024*1024;
extern "C" {
extern char *fake_heap_start,*fake_heap_end;
extern u32 __ctru_heap,__ctru_linear_heap;
void __system_allocateHeaps(){
    if(R_FAILED(svcControlMemory(&__ctru_heap,OS_HEAP_AREA_BEGIN,0,__ctru_heap_size,MEMOP_ALLOC,(MemPerm)(MEMPERM_READ|MEMPERM_WRITE))))svcBreak(USERBREAK_PANIC);
    if(R_FAILED(svcControlMemory(&__ctru_linear_heap,0,0,__ctru_linear_heap_size,MEMOP_ALLOC_LINEAR,(MemPerm)(MEMPERM_READ|MEMPERM_WRITE))))svcBreak(USERBREAK_PANIC);
    mappableInit(OS_MAP_AREA_BEGIN,OS_MAP_AREA_END);fake_heap_start=(char*)__ctru_heap;fake_heap_end=fake_heap_start+__ctru_heap_size;
}
}
namespace {
constexpr const char* ROOT="sdmc:/3ds/nfs_undercover";
PrintConsole console;
const devoptab_t* originalOutput=nullptr;
const devoptab_t* originalError=nullptr;
const devoptab_t* consoleOutput=nullptr;
devoptab_t quietOutput{},faultOutput{};
bool faultConsole=false;
ssize_t QuietWrite(struct _reent*,void*,const char*,size_t length){return length;}
void ActivateFaultConsole();
ssize_t FaultWrite(struct _reent* r,void* fd,const char* text,size_t length){
    ActivateFaultConsole();
    if(consoleOutput&&consoleOutput->write_r)consoleOutput->write_r(r,fd,text,length);
    gfxFlushBuffers();gfxScreenSwapBuffers(GFX_BOTTOM,false);
    return length;
}
void ActivateFaultConsole(){
    if(!faultConsole){
        faultConsole=true;consoleInit(GFX_BOTTOM,&console);consoleOutput=devoptab_list[STD_OUT];
        devoptab_list[STD_OUT]=&quietOutput;devoptab_list[STD_ERR]=&faultOutput;
    }
    consoleSelect(&console);
}
uint32_t bottomPrompts=0;
bool startupLoading=true;
bool wideMode=false;
constexpr unsigned kToggleLeft=0, kToggleTop=240-kToggleHeight;
void DrawBottomArt(){
    if(faultConsole)return;
    u8* screen=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,nullptr,nullptr);
    memcpy(screen,kBottomArt,sizeof(kBottomArt));
    const unsigned widths[]={83,160,77},bits[]={1,4,2};unsigned start=0,offset=0;
    for(unsigned p=0;p<3;++p){
        if(bottomPrompts&bits[p])for(unsigned y=0;y<17;++y)for(unsigned x=0;x<widths[p];++x){
            const u8* src=kPromptRgba+offset+(y*widths[p]+x)*4;
            u8* out=screen+((start+x)*240+239-y)*3;unsigned a=src[3];
            for(unsigned c=0;c<3;++c)out[c]=(unsigned(src[2-c])*a+unsigned(out[c])*(255-a)+127)/255;
        }
        start+=widths[p];offset+=widths[p]*17*4;
    }
    if(startupLoading)for(unsigned y=0;y<kStartupHeight;++y)for(unsigned x=0;x<kStartupWidth;++x){
        const u8* src=kStartupRgba+(y*kStartupWidth+x)*4;
        u8* out=screen+((320-kStartupWidth+x)*240+239-(240-kStartupHeight+y))*3;
        unsigned a=src[3];for(unsigned c=0;c<3;++c)out[c]=(unsigned(src[2-c])*a+unsigned(out[c])*(255-a)+127)/255;
    }
    if(!startupLoading){
        const u8* icon=wideMode?kSwitchToStandard:kSwitchToWide;
        for(unsigned y=0;y<kToggleHeight;++y)for(unsigned x=0;x<kToggleWidth;++x){
            const u8* src=icon+(y*kToggleWidth+x)*4;
            u8* out=screen+((kToggleLeft+x)*240+239-(kToggleTop+y))*3;
            const unsigned alpha=src[3];
            for(unsigned c=0;c<3;++c)out[c]=(unsigned(src[2-c])*alpha+unsigned(out[c])*(255-alpha)+127)/255;
        }
    }
    GSPGPU_FlushDataCache(screen,sizeof(kBottomArt));
    gfxScreenSwapBuffers(GFX_BOTTOM,false);
}
aptHookCookie appHookCookie;
void OnAppEvent(APT_HookType hook,void*){if(hook==APTHOOK_ONRESTORE)DrawBottomArt();}
void WaitAfterError(){
    std::fprintf(stderr,"\nPress A to return, or use HOME.\n");
    while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_A)break;gspWaitForVBlank();}
}

class Audio3DS {
    static constexpr unsigned COUNT=12,SAMPLES=512;
    ndspWaveBuf buffers[COUNT]{};s16* pcm=nullptr;unsigned next=0,queued=0,dropped=0;bool ready=false,playing=false;
public:
    bool Init(){
        Result rc=ndspInit();if(R_FAILED(rc)){printf("[Audio3DS] NDSP init failed: %08lx; continuing without output\n",(unsigned long)rc);return false;}
        pcm=(s16*)linearAlloc(COUNT*SAMPLES*2);
        if(!pcm){puts("[Audio3DS] PCM allocation failed");ndspExit();return false;}
        ready=true;ndspChnReset(0);ndspChnSetInterp(0,NDSP_INTERP_LINEAR);ndspChnSetFormat(0,NDSP_FORMAT_MONO_PCM16);ndspChnSetRate(0,22050.0f);
        float mix[12]{};mix[0]=mix[1]=1.0f;ndspChnSetMix(0,mix);ndspChnSetPaused(0,true);
        for(unsigned i=0;i<COUNT;++i)buffers[i].data_pcm16=pcm+i*SAMPLES;
        puts("[Audio3DS] NDSP ready: 22050 Hz mono");return true;
    }
    void Push(const int16_t* samples,size_t count,uint32_t rate){
        if(!ready)return;
        if(count>SAMPLES||rate!=22050){if(!dropped++)puts("[Audio3DS] Unexpected PCM format");return;}
        auto& b=buffers[next];
        if(b.status==NDSP_WBUF_QUEUED||b.status==NDSP_WBUF_PLAYING){if(++dropped==1||dropped%100==0)printf("[Audio3DS] Queue full; dropped blocks=%u\n",dropped);return;}
        for(size_t i=0;i<count;++i)b.data_pcm16[i]=NfsDoubleVolume(samples[i]);DSP_FlushDataCache(b.data_pcm16,count*2);b.nsamples=count;b.offset=0;b.looping=false;b.status=NDSP_WBUF_FREE;b.next=nullptr;
        ndspChnWaveBufAdd(0,&b);next=(next+1)%COUNT;++queued;
        if(!playing&&queued>=2){ndspChnSetPaused(0,false);playing=true;}
    }
    ~Audio3DS(){if(ready){ndspChnWaveBufClear(0);ndspChnReset(0);ndspExit();linearFree(pcm);printf("[Audio3DS] Submitted %u blocks; dropped %u\n",queued,dropped);}}
};
}
extern "C" void ShowLogOnScreen(){ActivateFaultConsole();}

int main(){
    gfxInitDefault();gfxSet3D(false);gfxSetScreenFormat(GFX_TOP,GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM,GSP_BGR8_OES);gfxSetDoubleBuffering(GFX_BOTTOM,false);
    originalOutput=devoptab_list[STD_OUT];originalError=devoptab_list[STD_ERR];
    quietOutput=*originalOutput;quietOutput.name="nfs_quiet";quietOutput.write_r=QuietWrite;
    faultOutput=*originalError;faultOutput.name="nfs_fault";faultOutput.write_r=FaultWrite;
    devoptab_list[STD_OUT]=&quietOutput;devoptab_list[STD_ERR]=&faultOutput;
    setvbuf(stdout,nullptr,_IONBF,0);setvbuf(stderr,nullptr,_IONBF,0);
    aptSetHomeAllowed(true);DrawBottomArt();gfxFlushBuffers();gspWaitForVBlank();aptHook(&appHookCookie,OnAppEvent,nullptr);
    std::error_code ec;std::filesystem::create_directories(ROOT,ec);
    int result=1;bool isNew=false;APT_CheckNew3DS(&isNew);
    if(!isNew)std::fprintf(stderr,"This application requires a New Nintendo 3DS / New 2DS XL.\n");
    else{
        osSetSpeedupEnable(true);uint64_t before=NfsClockMs();svcSleepThread(20000000);uint64_t elapsed=NfsClockMs()-before;
        printf("[3DS] Clock check: %llu ms\n",(unsigned long long)elapsed);
        if(elapsed<10||elapsed>1000)std::fprintf(stderr,"Clock check failed; game not started.\n");
        else{
            std::string error;auto cpu=Cpu::CreateNative(&error);
            if(!cpu)std::fprintf(stderr,"CPU: %s\n",error.c_str());
            else{
                S3EBinary binary;std::string assets=std::string(ROOT)+"/game";
                if(LoadS3E(assets+"/nfsuc.xe3u",binary)&&SetupSandbox(cpu.get(),binary,STOCK_BASE,64u<<20)){
                    bool pointersOK=true;for(const auto& p:binary.pointer_fixups){u32 actual=0;cpu->MemRead(G(binary.base_addr+p.offset),&actual,4);if(actual!=TRAMPOLINE_BASE+p.func_idx*8)pointersOK=false;}
                    printf("[3DS] Type-4 function pointers: %s (%u)\n",pointersOK?"verified":"FAILED",(unsigned)binary.pointer_fixups.size());
                    if(pointersOK){
                        S3ERuntime runtime(cpu.get(),assets,std::string(ROOT)+"/user");runtime.Init(binary.imports,binary.config);
                        Audio3DS audio;audio.Init();runtime.SetAudioCallback([&](const int16_t* p,size_t n,uint32_t rate){audio.Push(p,n,rate);});
                        puts("D-pad / Circle Pad: directions\nA: CENTER / confirm\nB: right soft key / back\nSTART: left soft key / pause\nX: keypad 5; Y: keypad 0\n");
                        runtime.SetPollCallback([&](S3ERuntime::Keys& keys){
                            keys.fill(false);if(!aptMainLoop()){runtime.RequestQuit();return;}hidScanInput();u32 held=hidKeysHeld();
                            if(!startupLoading&&(hidKeysDown()&KEY_TOUCH)){
                                touchPosition touch{};
                                hidTouchRead(&touch);
                                if(touch.px<kToggleLeft+kToggleWidth && touch.py>=kToggleTop && touch.py<240)
                                    runtime.RequestDisplayToggle();
                            }
                            keys[9]=(held&(KEY_DLEFT|KEY_CPAD_LEFT))!=0;keys[10]=(held&(KEY_DUP|KEY_CPAD_UP))!=0;keys[11]=(held&(KEY_DRIGHT|KEY_CPAD_RIGHT))!=0;keys[12]=(held&(KEY_DDOWN|KEY_CPAD_DOWN))!=0;
                            keys[78]=(held&KEY_A)!=0;keys[210]=(held&KEY_B)!=0;keys[209]=(held&KEY_START)!=0;keys[18]=(held&KEY_X)!=0;keys[13]=(held&KEY_Y)!=0;keys[20]=(held&KEY_L)!=0;keys[22]=(held&KEY_R)!=0;
                        });
                        runtime.SetFrameCallback([&](uint32_t,const uint16_t* pixels,S3ERuntime::Keys&){
                            const unsigned width=runtime.SurfaceWidth();
                            bool refreshBottom=bottomPrompts!=runtime.SoftkeyPrompts() || wideMode!=(width==400);
                            bottomPrompts=runtime.SoftkeyPrompts();
                            wideMode=width==400;
                            u8* screen=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,nullptr,nullptr);if(!wideMode)memset(screen,0,400*240*3);
                            const unsigned left=(400-width)/2;
                            for(unsigned y=0;y<240;++y)for(unsigned x=0;x<width;++x){u16 p=pixels[y*width+x];u8* out=screen+((x+left)*240+239-y)*3;out[0]=(p&31)*255/31;out[1]=((p>>5)&63)*255/63;out[2]=((p>>11)&31)*255/31;}
                            gfxFlushBuffers();gfxScreenSwapBuffers(GFX_TOP,false);
                            // The first guest presentation is its opening Loading screen.
                            // Hand over from our startup indicator only after submitting it.
                            if(startupLoading||refreshBottom){startupLoading=false;DrawBottomArt();}
                            gspWaitForVBlank();
                        });
                        result=runtime.Run(G(binary.entry_point));
                    }
                }
            }
        }
    }
    if(result!=0){std::fprintf(stderr,"\nUnable to continue (runtime result %d).\n",result);WaitAfterError();}
    fflush(nullptr);devoptab_list[STD_OUT]=originalOutput;devoptab_list[STD_ERR]=originalError;
    aptUnhook(&appHookCookie);gfxExit();return result;
}
