#include "gles.h"
#ifdef __3DS__
#include "gl3ds.h"
#endif
void InstallProbeHost();
#include "runtime_s3e.h"
#include "loader.h"
#include "heap.h"
#include "save_store.h"
#include "third_party/lzma/LzmaDec.h"
#include <algorithm>
#include "runtime_clock.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <vector>
#include <cctype>

namespace {
using U=uint32_t;using R=std::array<U,4>;using Done=std::function<void(U)>;
std::string lower(std::string s){for(char& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
void* LAlloc(ISzAllocPtr,size_t n){return std::malloc(n);}void LFree(ISzAllocPtr,void* p){std::free(p);}const ISzAlloc kLzmaAlloc={LAlloc,LFree};
}
struct S3ERuntime::Impl {
    Cpu* cpu;std::string assets,saves;GuestHeap heap;std::vector<std::string> imports;
#ifdef __3DS__
    U cruiserBannerOwner = 0;

    void observeRaceBanner() {
        // The totaled cruiser caller composes localization IDs 141 and 142.
        // All language variants use this same notification path.
        cruiserBannerOwner = reg(Cpu::REG_LR) == G(0x4a083ed9) ? reg(Cpu::REG_R0) : 0;
    }

    void updateCruiserBanner() {
        int32_t remaining = 0;
        if (cruiserBannerOwner &&
            (cpu->MemRead(cruiserBannerOwner + 0x6c0, &remaining, sizeof(remaining)) != 0 ||
             remaining <= 0 || remaining > 1500)) cruiserBannerOwner = 0;
        GL3DS::SetHideMinimap(cruiserBannerOwner != 0);
    }
#endif
    U secureError=0;
    std::map<std::string,std::string> config;std::array<U,3> memmgr{};
    struct File{std::vector<uint8_t> data;size_t pos=0;U backing=0;};std::map<U,File> files;
    std::vector<std::array<U,11>> fs;std::set<size_t> activeFS;std::map<U,size_t> handles;
    struct Regs{std::array<U,17> v;};struct Task{Regs regs;Done done;};std::vector<Task> tasks;
    struct Decoder {
        CLzmaDec dec{};bool ready=false;U reader=0,user=0,request=0,buffer=0;uint64_t remaining=0;
        std::vector<uint8_t> in;size_t pos=0;
        Decoder(){LzmaDec_Construct(&dec);}~Decoder(){if(ready)LzmaDec_Free(&dec,&kLzmaAlloc);}
    };std::map<U,std::shared_ptr<Decoder>> decoders;
    std::map<U,std::pair<U,U>> keyCallbacks,deviceCallbacks,pointerCallbacks;
    static constexpr U soundRate=22050,soundBlock=512,soundChannels=16;
    struct Channel {bool playing=false,paused=false,suppressed=false;U rate=8000,volume=256,uservar=0,effectWave=0,start=0,length=0;int32_t repeat=1,loop=0;double pos=0;std::array<U,4> fn{},ud{};};
    std::array<Channel,soundChannels> channels{};U masterVolume=256,mixBuf=0,genInfo=0,endInfo=0;bool mixing=false;uint64_t audioSamples=0;U audioStart=0;bool audioStarted=false;
    std::function<void(const int16_t*,size_t,U)> audioOut;
    std::function<void(U,const int16_t*,size_t,U)> audioStemOut;
    std::array<std::vector<int16_t>,soundChannels> audioStems;
    uint64_t audioWritten=0;
    bool traceAudio=std::getenv("NFS_AUDIO_TRACE")!=nullptr;
    std::array<int32_t,soundBlock> mixAccumulator{};
    std::array<int16_t,soundBlock> outputBlock{},channelScratch{};
    U soundStarts=0,fileApiOpens=0,hostReads=0,mixBlocks=0,generatorCalls=0,maxGeneratorMs=0,maxMixMs=0,mixBegin=0;
    struct Gap{U frame=0,ms=0,starts=0,opens=0;};std::array<Gap,8> worstGaps{};
    U lastFrameTime=0,lastFrameStarts=0,lastFrameOpens=0;
    void measureFrame(){U now=ms();if(lastFrameTime){Gap g{frames,U(now-lastFrameTime),U(soundStarts-lastFrameStarts),U(fileApiOpens-lastFrameOpens)};if(g.ms>worstGaps.back().ms){worstGaps.back()=g;std::sort(worstGaps.begin(),worstGaps.end(),[](const Gap&a,const Gap&b){return a.ms>b.ms;});}}lastFrameTime=now;lastFrameStarts=soundStarts;lastFrameOpens=fileApiOpens;}
    void reportPerformance(){std::fprintf(stdout,"[Perf] soundStarts=%u fileOpens=%u hostReads=%u mixBlocks=%u generatorCalls=%u maxGeneratorMs=%u maxMixMs=%u\n",soundStarts,fileApiOpens,hostReads,mixBlocks,generatorCalls,maxGeneratorMs,maxMixMs);for(const auto&g:worstGaps)if(g.frame)std::fprintf(stdout,"[PerfGap] frame=%u gapMs=%u soundStarts=%u fileOpens=%u\n",g.frame,g.ms,g.starts,g.opens);}

    void audioEvent(const char* event,U ch){if(!traceAudio)return;std::fprintf(stdout,"[AudioEvent] record=%.6f frame=%u ms=%u event=%s ch=%u generator=%08x\n",double(audioWritten)/soundRate,frames,ms(),event,ch,ch<soundChannels?channels[ch].fn[1]:0);}


    U prompts=0;
    bool effectGenerator(U fn){return fn==G(0x4a04c31d)||fn==G(0x4a0a1e2b);}
    U effectWave(const Channel& c){
        // The PCM generator does not set S3E's USER_VAR. That field may still
        // name a previous ADPCM sound on a recycled channel. IwSound's end
        // callback owns a voice whose +0xc is the sound specification; its
        // +0x3c is the actual wave resource. Snapshot it before playback.
        if(c.fn[0]==G(0x4a024339)&&c.ud[0]){
            U owner=word(c.ud[0]+0xc);if(owner)return word(owner+0x3c);
        }
        return c.fn[1]==G(0x4a04c31d)?c.uservar:0;
    }
    void guardEffectStart(){
        // This is IwSound's start request, before channel selection, decoder reset
        // or replacement. A null result is its existing 'no voice started' path.
        // Compare wave resources rather than voice/channel identity: a new request
        // can otherwise replace the decoder that is already playing this effect.
        U sound=reg(Cpu::REG_R0);
        if(!sound)return;
        U wave=word(sound+0x3c);
        if(!wave)return;
        for(U ch=0;ch<soundChannels;++ch){const auto& c=channels[ch];
            if(c.playing&&!c.suppressed&&effectGenerator(c.fn[1])&&c.repeat==0&&c.effectWave==wave){
                if(traceAudio)fprintf(stdout,"[AudioStartGuard] frame=%u wave=%08x activeChannel=%u caller=%08x\n",frames,wave,ch,reg(Cpu::REG_LR));
                finish(0);return;
            }
        }
    }
    U playerModelRoot = 0;

    U optionalWord(U address) {
        U value = 0;
        if (address) cpu->MemRead(address, &value, sizeof(value));
        return value;
    }

    void trackPlayerModel() {
        // The race player update reads its active vehicle at +0x2d4.
        const U player = optionalWord(reg(Cpu::REG_R0) + 0x2d4);
        const U renderState = player && optionalWord(player + 16) == 0
            ? optionalWord(player + 0x220) : 0;
        playerModelRoot = optionalWord(optionalWord(renderState));
    }

    void keepPlayerModelDetailed() {
        if (surfaceWidth != 400 || !playerModelRoot || !reg(Cpu::REG_R0)) return;

        // The renderable model owns a scene node. Follow its parents to
        // scope the distance override to the active player's model tree.
        U node = optionalWord(reg(Cpu::REG_R4) + 28);
        for (unsigned depth = 0; node && depth < 16; ++depth) {
            if (node == playerModelRoot) {
                const U renderable = optionalWord(reg(Cpu::REG_SP) + 52);
                const U modelSet = optionalWord(renderable ? renderable + 40 : 0);
                if (!modelSet) return;

                const U models = optionalWord(modelSet + 36);
                const U count = optionalWord(modelSet + 40);
                const U lodGroup = optionalWord(modelSet + 68);
                const U order = lodGroup ? optionalWord(modelSet + 52) : 0;
                if (!models || (lodGroup && !order)) return;

                const U index = optionalWord(order);
                if (index < count) {
                    const U highModel = optionalWord(models + 4 * index);
                    if (highModel) set(Cpu::REG_R0, highModel);
                }
                return;
            }
            node = optionalWord(node + 120);
        }
    }

    void softkeys(){
        U obj=reg(Cpu::REG_R0),left=word(obj+0x714),right=word(obj+0x710);
        call(G(0x4a076979),{left},[this,right,left](U a){
            call(G(0x4a076979),{right},[this,a,left,right](U b){
                // The game's symbol atlas: 0=check, 1=back, 2=play, 3=X, 4=pause.
                U next=0;for(U icon:{a,b}){if(icon==0||icon==2)next|=1;if(icon==1||icon==3)next|=2;if(icon==4)next|=4;}
                if(getenv("NFS_PROMPT_TRACE")&&next!=prompts)fprintf(stdout,"[Prompts] frame=%u raw=%x,%x icons=%d,%d mask=%u\n",frames,left,right,int(a),int(b),next);
                prompts=next;finish(0);
            });
        });
    }
    std::array<U,256> keyState{};Keys desired{};
    std::function<void(U,const uint16_t*,Keys&)> present;
    uint64_t start=NfsClockMs();U lastPoll=0,lastHeartbeat=0,pollCounter=0;
    std::function<void(Keys&)> poll;
    std::set<std::string> seen;bool quit=false,failed=false;U frames=0,limit=0;uint64_t calls=0;
    explicit Impl(Cpu* c,std::string a,std::string s):cpu(c),assets(std::move(a)),saves(std::move(s)){heap.Init(HEAP_BASE+256,HEAP_SIZE-256);std::error_code ec;std::filesystem::create_directories(saves,ec);}
    void fatal(const std::string& msg){if(!failed)std::fprintf(stderr,"[S3E] ERROR: %s\n",msg.c_str());failed=quit=true;cpu->Stop();}
    U reg(int n){U x=0;cpu->RegRead(n,&x);return x;}
    void set(int n,U x){cpu->RegWrite(n,&x);}
    void rd(U a,void* dst,size_t n){if(n&&cpu->MemRead(a,dst,n)!=Cpu::OK){std::memset(dst,0,n);fatal("unmapped read "+std::to_string(a));}}
    void wr(U a,const void* src,size_t n){if(n&&cpu->MemWrite(a,src,n)!=Cpu::OK)fatal("unmapped write "+std::to_string(a));}
    U word(U a){U x=0;rd(a,&x,4);return x;}void word(U a,U x){wr(a,&x,4);}
    std::string str(U a,size_t cap=4096){std::string s;if(!a)return s;for(size_t k=0;k<cap;++k){char c=0;rd(a+k,&c,1);if(!c)break;s+=c;}return s;}
    void zero(U a,U n){static const std::array<uint8_t,4096> z{};while(n){U b=std::min<U>(n,z.size());wr(a,z.data(),b);a+=b;n-=b;}}
    U alloc(U n){U a=heap.Alloc(n);if(getenv("NFS_DIAG"))fprintf(stderr,"[Alloc] %u -> %08x lr=%08x\n",n,a,reg(Cpu::REG_LR));if(a)zero(a,heap.SizeOf(a));else std::fprintf(stderr,"[S3E] allocation failed: %lu bytes; largest %lu\n",(unsigned long)n,(unsigned long)heap.LargestFree());return a;}
    U realloc_(U a,U n){if(getenv("NFS_DIAG"))fprintf(stderr,"[Realloc] %08x -> %u lr=%08x\n",a,n,reg(Cpu::REG_LR));U old=heap.SizeOf(a),from=0,count=0;U result=heap.Realloc(a,n,&from,&count);if(result&&from){std::vector<uint8_t> data(count);rd(from,data.data(),count);wr(result,data.data(),count);heap.Free(from);if(heap.SizeOf(result)>count)zero(result+count,heap.SizeOf(result)-count);}else if(result&&heap.SizeOf(result)>old)zero(result+old,heap.SizeOf(result)-old);return result;}
    void pc(U target){U flags=reg(Cpu::REG_CPSR);flags=(flags&~32u)|((target&1)?32u:0);set(Cpu::REG_CPSR,flags);set(Cpu::REG_PC,target);}
    void finish(U v){set(Cpu::REG_R0,v);pc(reg(Cpu::REG_LR));}
    Regs save(){Regs s;for(int k=0;k<17;++k)s.v[k]=reg(k);return s;}
    void restore(const Regs& s){for(int k=0;k<15;++k)set(k,s.v[k]);set(Cpu::REG_CPSR,s.v[16]);set(Cpu::REG_PC,s.v[15]|((s.v[16]&32)?1:0));}
    void call(U fn,std::vector<U> a,Done done){if(!fn){done(0);return;}tasks.push_back({save(),std::move(done)});U sp=reg(Cpu::REG_SP)-32;set(Cpu::REG_SP,sp);for(size_t k=0;k<a.size();++k){if(k<4)set(k,a[k]);else word(sp+4*(k-4),a[k]);}set(Cpu::REG_LR,TRAMPOLINE_BASE+imports.size()*8);pc(fn);}
    void returned(){if(tasks.empty()){fatal("orphan callback return");return;}U result=reg(Cpu::REG_R0);Task t=std::move(tasks.back());tasks.pop_back();restore(t.regs);t.done(result);}
    std::string norm(std::string p){std::replace(p.begin(),p.end(),'\\','/');p=lower(p);for(auto pre:{"rom:/","raw:/","ram:/","data:/"})if(p.rfind(pre,0)==0){p=p.substr(std::strlen(pre));break;}while(!p.empty()&&p[0]=='/')p.erase(p.begin());for(size_t at;(at=p.find("//"))!=std::string::npos;)p.erase(at,1);return p;}
    U hostOpen(const std::string& name,const std::string& mode){if(mode!="r"&&mode!="rb")return 0;std::string p=norm(name);if(p.find("..")!=std::string::npos)return 0;std::ifstream f(assets+"/"+p,std::ios::binary);if(!f)return 0;++hostReads;File file;file.data.assign(std::istreambuf_iterator<char>(f),{});U h=alloc(64);if(!h)return 0;files[h]=std::move(file);return h;}
    struct Open{R r;size_t next=0;Done done;};
    void openStep(std::shared_ptr<Open> s){while(s->next<fs.size()){size_t n=s->next++;if(activeFS.count(n)||!fs[n][0])continue;activeFS.insert(n);call(fs[n][0],{s->r[0],s->r[1]},[this,s,n](U h){activeFS.erase(n);if(h){handles[h]=n;s->done(h);}else openStep(s);});return;}s->done(hostOpen(str(s->r[0]),str(s->r[1])));}
    void open(R r,Done done){std::string path=norm(str(r[0]));U override=hostOpen("overrides/"+path,str(r[1]));if(override){std::fprintf(stdout,"[Asset override] %s\n",path.c_str());done(override);return;}auto s=std::make_shared<Open>();s->r=r;U name=alloc(path.size()+1);if(!name){done(0);return;}wr(name,path.c_str(),path.size()+1);s->r[0]=name;s->done=[this,name,done](U h){heap.Free(name);done(h);};openStep(s);}
    void close(U h,Done done){auto f=files.find(h);if(f!=files.end()){files.erase(f);heap.Free(h);done(0);return;}auto g=handles.find(h);if(g==handles.end()){done(1);return;}size_t n=g->second;handles.erase(g);call(fs[n][5],{h},std::move(done));}
    void read(R r,Done done){auto g=handles.find(r[3]);if(g!=handles.end()){call(fs[g->second][1],{r[0],r[1],r[2],r[3]},std::move(done));return;}auto f=files.find(r[3]);if(f==files.end()){done(0);return;}File& file=f->second;size_t n=std::min<uint64_t>(uint64_t(r[1])*r[2],file.pos<file.data.size()?file.data.size()-file.pos:0);wr(r[0],file.data.data()+std::min(file.pos,file.data.size()),n);file.pos+=n;done(r[1]?n/r[1]:0);}
    void seek(R r,Done done){auto g=handles.find(r[0]);if(g!=handles.end()){call(fs[g->second][3],{r[0],r[1],r[2]},std::move(done));return;}auto f=files.find(r[0]);if(f==files.end()||r[2]>2){done(1);return;}File& file=f->second;int64_t pos=(r[2]==0?0:r[2]==1?file.pos:file.data.size())+int64_t(int32_t(r[1]));if(pos<0){done(1);return;}file.pos=pos;done(0);}
    void tell(U h,Done done){auto g=handles.find(h);if(g!=handles.end()){call(fs[g->second][4],{h},std::move(done));return;}auto f=files.find(h);done(f==files.end()?0:f->second.pos);}
    void getSize(U h,Done done){auto f=files.find(h);if(f!=files.end()){done(f->second.data.size());return;}tell(h,[this,h,done](U old){seek({h,0,2,0},[this,h,old,done](U){tell(h,[this,h,old,done](U n){seek({h,old,0,0},[done,n](U){done(n);});});});});}
    struct DecompRead{std::shared_ptr<Decoder> d;U dst=0,countptr=0;size_t need=0;std::vector<uint8_t> out;};
    void decompFinish(std::shared_ptr<DecompRead> s,U error=0){if(getenv("NFS_RESOURCE_TRACE")&&s->out.size()>=4&&s->out[0]==0x3d){fprintf(stderr,"[Decomp] dst=%08x size=%zu bytes=",s->dst,s->out.size());for(size_t i=0;i<std::min<size_t>(s->out.size(),256);++i)fprintf(stderr,"%02x",s->out[i]);fprintf(stderr,"\n");}wr(s->dst,s->out.data(),s->out.size());word(s->countptr,s->out.size());finish(error);}
    void decompStep(std::shared_ptr<DecompRead> s){auto d=s->d;
        for(;;){
            if(s->out.size()>=s->need){decompFinish(s);return;}
            if(d->ready&&(s->out.size()>=s->need||d->remaining==0)){decompFinish(s);return;}
            if(d->pos>=d->in.size()){
                word(d->request,d->buffer);word(d->request+4,4096);
                call(d->reader,{d->request,d->user},[this,s,d](U n){if(!n||n>4096){decompFinish(s,1);return;}d->in.resize(n);rd(d->buffer,d->in.data(),n);d->pos=0;decompStep(s);});return;
            }
            if(!d->ready){if(d->in.size()-d->pos<13){decompFinish(s,1);return;}const uint8_t* p=d->in.data()+d->pos;d->remaining=0;for(int k=0;k<8;++k)d->remaining|=uint64_t(p[5+k])<<(8*k);if(LzmaDec_Allocate(&d->dec,p,5,&kLzmaAlloc)!=SZ_OK){decompFinish(s,1);return;}d->ready=true;LzmaDec_Init(&d->dec);d->pos+=13;s->need=std::min<uint64_t>(s->need,d->remaining);}
            size_t prev=s->out.size();SizeT produced=std::min<uint64_t>(s->need-prev,d->remaining);SizeT consumed=d->in.size()-d->pos;s->out.resize(prev+produced);ELzmaStatus status;
            SRes rc=LzmaDec_DecodeToBuf(&d->dec,s->out.data()+prev,&produced,d->in.data()+d->pos,&consumed,LZMA_FINISH_ANY,&status);s->out.resize(prev+produced);d->pos+=consumed;d->remaining-=produced;
            if(rc!=SZ_OK){decompFinish(s,1);return;}if(!produced&&!consumed){decompFinish(s,1);return;}
        }
    }
    void updateKeys(){auto changes=std::make_shared<std::vector<std::pair<U,bool>>>();for(U k=0;k<256;++k){bool prev=keyState[k]&1,now=desired[k];keyState[k]=(now?1u:0u)|(now&&!prev?2u:0u)|(prev&&!now?4u:0u);if(prev!=now)changes->push_back({k,now});}keyStep(changes,0);}
    void keyStep(std::shared_ptr<std::vector<std::pair<U,bool>>> c,size_t at){auto cb=keyCallbacks.find(0);if(at>=c->size()||cb==keyCallbacks.end()){finish(0);return;}U p=alloc(8);word(p,(*c)[at].first);word(p+4,(*c)[at].second);auto fn=cb->second;call(fn.first,{p,fn.second},[this,c,at,p](U){heap.Free(p);keyStep(c,at+1);});}
    void audioEnd(U ch,Done done){auto& c=channels[ch];audioEvent(c.suppressed?"end-suppressed":"end",ch);c.playing=false;if(c.fn[0]){word(endInfo,ch);word(endInfo+4,0);call(c.fn[0],{endInfo,c.ud[0]},[done](U){done(0);});}else done(0);}
    void audioChannel(U ch,Done done){
        if(ch==soundChannels){auto& block=outputBlock;for(U i=0;i<soundBlock;++i)block[i]=static_cast<int16_t>(std::clamp<int64_t>(int64_t(mixAccumulator[i])*masterVolume/256,-32768,32767));if(audioStemOut)for(U ch=0;ch<soundChannels;++ch){for(auto& sample:audioStems[ch])sample=static_cast<int16_t>(int32_t(sample)*masterVolume/256);audioStemOut(ch,audioStems[ch].data(),soundBlock,soundRate);}if(audioOut)audioOut(block.data(),block.size(),soundRate);audioSamples+=soundBlock;audioWritten+=soundBlock;++mixBlocks;maxMixMs=std::max(maxMixMs,U(ms()-mixBegin));mixing=false;done(0);return;}
        auto next=[this,ch,done](U){audioChannel(ch+1,done);};auto& c=channels[ch];
        if(!c.playing||c.paused){next(0);return;}
        if(c.fn[2]&&!c.fn[1]){fatal("stereo audio generator not yet supported");return;}
        if(c.fn[1]){
            // Each generator owns a clean target. Some NFS generators overwrite it
            // even when the old shared-target mix flag was set.
            zero(mixBuf,soundBlock*2);U info[9]={ch,mixBuf,soundBlock,0,c.start,c.length,static_cast<U>(c.repeat),0,0};wr(genInfo,info,sizeof(info));
            U begin=ms();++generatorCalls;call(c.fn[1],{genInfo,c.ud[1]},[this,ch,next,begin](U){maxGeneratorMs=std::max(maxGeneratorMs,U(ms()-begin));auto& samples=channelScratch;rd(mixBuf,samples.data(),samples.size()*2);if(!channels[ch].suppressed){for(U i=0;i<soundBlock;++i)mixAccumulator[i]+=samples[i];if(audioStemOut)audioStems[ch].assign(samples.begin(),samples.end());}if(word(genInfo+28))audioEnd(ch,next);else next(0);});return;
        }
        if(!c.start||!c.length){audioEnd(ch,next);return;}
        if(c.length>16u*1024*1024){fatal("invalid PCM sample length");return;}
        std::vector<int16_t> src(c.length);rd(c.start,src.data(),src.size()*2);bool ended=false;
        for(U i=0;i<soundBlock;++i){if(c.pos>=c.length){if(c.repeat==1){ended=true;break;}if(c.repeat>1)--c.repeat;c.pos=std::clamp<int64_t>(c.loop,0,c.length-1);}int32_t sample=int32_t(src[static_cast<size_t>(c.pos)])*c.volume/256;mixAccumulator[i]+=sample;if(audioStemOut)audioStems[ch][i]=static_cast<int16_t>(sample);c.pos+=double(c.rate)/soundRate;}
        if(ended)audioEnd(ch,next);else next(0);
    }
    void pumpAudio(Done done,unsigned blocks=0){
        if(mixing||!audioStarted||blocks>=4){done(0);return;}
        uint64_t due=uint64_t(U(ms()-audioStart))*soundRate/1000;
        if(audioSamples+soundBlock>due){done(0);return;}
        // Bound callback work after long file loads; retain a short audible gap instead of a callback storm.
        if(due>audioSamples+soundRate/4)audioSamples=due-soundRate/4;
        if(!mixBuf){mixBuf=alloc(soundBlock*2);genInfo=alloc(36);endInfo=alloc(8);}
        mixing=true;mixBegin=ms();mixAccumulator.fill(0);if(audioStemOut)for(auto& stem:audioStems)stem.assign(soundBlock,0);audioChannel(0,[this,done,blocks](U){pumpAudio(done,blocks+1);});
    }
    bool number(const std::string& text,int64_t& value,unsigned depth=0){
        if(depth>16)return false;
        size_t at=0;bool ok=true;
        auto space=[&](){while(at<text.size()&&std::isspace(static_cast<unsigned char>(text[at])))++at;};
        std::function<int64_t()> sum,product,atom;
        atom=[&]()->int64_t{space();if(at>=text.size()){ok=false;return 0;}
            if(text[at]=='+'||text[at]=='-'){bool negative=text[at++]=='-';auto n=atom();return negative?-n:n;}
            if(text[at]=='('){++at;auto n=sum();space();if(at>=text.size()||text[at++]!=')')ok=false;return n;}
            if(text[at]=='['){size_t end=text.find(']',++at);if(end==std::string::npos){ok=false;return 0;}std::string section=text.substr(at,end-at);at=end+1;space();size_t begin=at;while(at<text.size()&&(std::isalnum(static_cast<unsigned char>(text[at]))||text[at]=='_'))++at;if(at==begin){ok=false;return 0;}auto it=config.find(lower(section+"/"+text.substr(begin,at-begin)));int64_t n=0;if(it==config.end()||!number(it->second,n,depth+1))ok=false;return n;}
            char* end=nullptr;const char* begin=text.c_str()+at;auto n=std::strtoll(begin,&end,0);if(end==begin){ok=false;return 0;}at=end-text.c_str();return n;
        };
        product=[&]()->int64_t{auto n=atom();for(;;){space();if(at>=text.size()||(text[at]!='*'&&text[at]!='/'&&text[at]!='%'))return n;char op=text[at++];auto r=atom();if((op=='/'||op=='%')&&!r){ok=false;return 0;}n=op=='*'?n*r:op=='/'?n/r:n%r;}};
        sum=[&]()->int64_t{auto n=product();for(;;){space();if(at>=text.size()||(text[at]!='+'&&text[at]!='-'))return n;char op=text[at++];auto r=product();n=op=='+'?n+r:n-r;}};
        value=sum();space();return ok&&at==text.size();
    }
    void parse(const std::string& text){std::string section;bool active=true;size_t p=0;while(p<text.size()){size_t e=text.find('\n',p);std::string line=trim(text.substr(p,e==std::string::npos?e:e-p));p=e==std::string::npos?text.size():e+1;if(line.empty()||line[0]=='#'||line.rfind("//",0)==0)continue;if(line[0]=='{'){std::string cond=lower(line.substr(1,line.find('}')-1));cond.erase(std::remove_if(cond.begin(),cond.end(),[](char c){return c==' '||c=='\t';}),cond.end());active=cond.empty()||cond=="id=any"||cond=="class=any"||cond=="os=ngi"||cond=="os=symbian";continue;}if(line[0]=='['){section=lower(line.substr(1,line.find(']')-1));continue;}size_t eq=line.find('=');if(active&&eq!=std::string::npos){std::string val=trim(line.substr(eq+1));if(!val.empty()&&val[0]=='"')val=val.substr(1,val.find('"',1)-1);else val=trim(val.substr(0,val.find('#')));int64_t resolved=0;if(val.find('[')!=std::string::npos&&number(val,resolved))val=std::to_string(resolved);config[section+"/"+lower(trim(line.substr(0,eq)))]=val;}}}
    U ms(){return static_cast<U>(NfsClockMs()-start);}
    void pollHost(bool force=false){U now=ms();if(poll&&(force||U(now-lastPoll)>=16)){lastPoll=now;poll(desired);}if(U(now-lastHeartbeat)>=5000){lastHeartbeat=now;std::fprintf(stdout,"[S3E] Heartbeat ms=%lu frame=%lu calls=%llu callbacks=%lu\n",(unsigned long)now,(unsigned long)frames,(unsigned long long)calls,(unsigned long)tasks.size());}}
    std::vector<std::pair<U,U>> surfaceCallbacks;
    unsigned surfaceWidth = 320;
    bool surfaceToggleRequested = false;
    bool hardwareRenderer = false;
    void notifySurface(const std::shared_ptr<std::vector<std::pair<U,U>>>& callbacks, size_t at, U event, U returnValue){
        if(at==callbacks->size()){
            heap.Free(event);
            finish(returnValue);
            return;
        }
        const auto [fn,user]=(*callbacks)[at];
        call(fn,{event,user},[this,callbacks,at,event,returnValue](U){
            notifySurface(callbacks,at+1,event,returnValue);
        });
    }
    bool switchSurface(U returnValue=0){
        if(!surfaceToggleRequested)return false;
        surfaceToggleRequested=false;
        if(surfaceCallbacks.empty())return false;
        U event=alloc(20);
        if(!event)return false;
        surfaceWidth=surfaceWidth==320?400:320;
        const U change[]={1,surfaceWidth,240,0,2*surfaceWidth};
        wr(event,change,sizeof(change));
        if (hardwareRenderer) {
            constexpr unsigned outputWidth = 400;
            GLES::Instance().SetDisplay(surfaceWidth,240,surfaceWidth,240,outputWidth,240);
#ifdef __3DS__
            GL3DS::SetSceneWidth(surfaceWidth);
#endif
        }
        notifySurface(std::make_shared<std::vector<std::pair<U,U>>>(surfaceCallbacks),0,event,returnValue);
        return true;
    }
    void svc(U index){if((++pollCounter&63u)==0)pollHost();if(quit){finish(0);cpu->Stop();return;}if(index==imports.size()){returned();return;}if(index>=imports.size()){fatal("invalid service index");return;}++calls;const std::string& n=imports[index];if(seen.insert(n).second)std::fprintf(stdout,"[S3E] Startup import %llu: %s\n",(unsigned long long)calls,n.c_str());R r{reg(0),reg(1),reg(2),reg(3)};if(getenv("NFS_DIAG")&&(n=="s3eKeyboardGetInt"||n=="s3ePointerGetInt"||n=="s3eMemorySetUserMemMgr"))fprintf(stderr,"[Capability] %s a0=%u a1=%08x a2=%08x\n",n.c_str(),r[0],r[1],r[2]);if(hardwareRenderer&&(n.rfind("gl",0)==0||n.rfind("egl",0)==0)){
#ifdef __3DS__
        if(n=="eglSwapBuffers") updateCruiserBanner();
#endif
        U ret=0;
        if(GLES::Instance().Handle(n,cpu,r.data(),ret)){
            if(!quit&&n=="eglSwapBuffers"&&ret&&switchSurface(ret))return;
            finish(ret);
            if(quit)cpu->Stop();
            return;
        }
    }
auto done=[this](U x){finish(x);};
        if(n=="s3eMalloc"||n=="s3eMallocBase"){finish(alloc(r[0]));return;}
        if(n=="s3eRealloc"||n=="s3eReallocBase"){finish(realloc_(r[0],r[1]));return;}
        if(n=="s3eFree"||n=="s3eFreeBase"){heap.Free(r[0]);finish(0);return;}
        if(n=="s3eMemoryGetInt"){U x=0;if(r[0]==1)x=32u<<20;else if(r[0]==2)x=std::min<U>(32u<<20,heap.FreeBytes());else if(r[0]==3)x=heap.BytesInUse();else if(r[0]==4)x=std::min<U>(32u<<20,heap.LargestFree());finish(x);return;}
        if(n=="s3eMemorySetUserMemMgr"){if(r[0])rd(r[0],memmgr.data(),12);else memmgr.fill(0);finish(0);return;}
        if(n=="s3eMemoryGetUserMemMgr"){wr(r[0],memmgr.data(),12);finish(0);return;}
        if(n=="s3eConfigGetInt"||n=="s3eConfigGetString"){auto i=config.find(lower(str(r[0])+"/"+str(r[1])));if(getenv("NFS_DIAG"))fprintf(stderr,"[Config] %s/%s = %s\n",str(r[0]).c_str(),str(r[1]).c_str(),i==config.end()?"<unset>":i->second.c_str());if(i==config.end()){finish(1);return;}if(n=="s3eConfigGetInt"){int64_t resolved=0;if(!number(i->second,resolved)){finish(1);return;}word(r[2],static_cast<U>(resolved));}else{auto s=i->second.substr(0,254);wr(r[2],s.c_str(),s.size()+1);}finish(0);return;}
        if(n=="s3eTimerGetMs"){uint64_t now=NfsClockMs()-start;set(1,U(now>>32));finish(U(now));return;}if(n=="s3eTimerGetInt"){finish(r[0]==0?1:0);return;}
        if(n=="s3eGLGetNativeWindow"&&hardwareRenderer){finish(1);return;}
        if(n=="s3eGLGetInt"&&hardwareRenderer){finish(r[0]==0?0x110:0);return;}
        if(n=="s3eAccelerometerGetInt"||n=="s3eDeviceGetInt"||n=="s3eGLGetInt"||n=="s3eAudioGetInt"||n=="s3eAudioIsCodecSupported"){finish(0);return;}
        if(n=="s3eAccelerometerStart"){finish(1);return;}
        if(n=="s3eAccelerometerStop"){finish(0);return;}
        if(n=="s3eAccelerometerGetX"||n=="s3eAccelerometerGetY"||n=="s3eAccelerometerGetZ"){finish(0);return;}
        if(n=="s3eDeviceRegister"){deviceCallbacks[r[0]]={r[1],r[2]};finish(0);return;}
        if(n=="s3eDeviceUnRegister"){deviceCallbacks.erase(r[0]);finish(0);return;}
        if(n=="s3eDeviceCheckQuitRequest"){finish(quit);return;}
        if(n=="s3eDeviceExit"||n=="s3eDeviceRequestQuit"){finish(0);quit=true;cpu->Stop();return;}
        if(n=="s3eSurfaceRegister"){
            if(r[0]==1 && r[1])surfaceCallbacks.emplace_back(r[1],r[2]);
            finish(0);return;
        }
        if(n=="s3eSurfaceUnRegister"){
            if(r[0]==1)surfaceCallbacks.erase(std::remove_if(surfaceCallbacks.begin(),surfaceCallbacks.end(),[&](const auto& cb){return cb.first==r[1];}),surfaceCallbacks.end());
            finish(0);return;
        }
        if(n=="s3eSurfaceGetInt"){finish(r[0]==0?surfaceWidth:r[0]==1?240:r[0]==2?surfaceWidth*2:r[0]==8?1:0);return;}
        if(n=="s3eSurfacePtr"){finish(FB_BASE);return;}
        if(n=="s3eSurfaceShow"){++frames;measureFrame();std::vector<uint16_t> image(surfaceWidth*240);rd(FB_BASE,image.data(),image.size()*2);if(present)present(frames,image.data(),desired);if(frames==1||frames%300==0)std::fprintf(stdout,"[S3E] Frame %lu; heap %lu KB; calls %llu\n",(unsigned long)frames,(unsigned long)(heap.BytesInUse()/1024),(unsigned long long)calls);if(!quit&&(!limit||frames<limit)&&switchSurface())return;finish(0);if(limit&&frames>=limit){quit=true;cpu->Stop();}return;}
        if(n=="s3ePointerRegister"){pointerCallbacks[r[0]]={r[1],r[2]};finish(0);return;}
        if(n=="s3ePointerUnRegister"){pointerCallbacks.erase(r[0]);finish(0);return;}
        if(n=="s3eKeyboardRegister"){keyCallbacks[r[0]]={r[1],r[2]};finish(0);return;}if(n=="s3eKeyboardUnRegister"){keyCallbacks.erase(r[0]);finish(0);return;}
        if(n=="s3eKeyboardUpdate"){updateKeys();return;}if(n=="s3eKeyboardGetState"){if(getenv("NFS_KEY_TRACE")){static std::set<U> codes;if(codes.insert(r[0]).second)fprintf(stderr,"[KeyQuery] code=%u caller=%08x\n",r[0],reg(Cpu::REG_LR));}finish(r[0]<256?keyState[r[0]]:0);return;}
        if(n=="s3eKeyboardGetDisplayName"){
            // Observed guest ABI: output buffer, S3E key, display-format flag.
            const std::map<U,std::string> labels={{9,"LEFT"},{10,"UP"},{11,"RIGHT"},{12,"DOWN"},{73,"LSK"},{72,"RSK"},{78,"OK"},{200,"A"},{201,"B"}};
            auto it=labels.find(r[1]);std::string label=it==labels.end()?"?":it->second;if(r[1]>=13&&r[1]<=22)label=std::string(1,static_cast<char>('0'+r[1]-13));
            std::fprintf(stdout,"[S3E] Key label %u -> %s (format %u)\n",r[1],label.c_str(),r[2]);
            wr(r[0],label.c_str(),label.size()+1);finish(0);return;
        }
        if(n=="s3eKeyboardAnyKey"){U v=0;for(U k=1;k<256;++k)if(keyState[k]&3){v=k;break;}finish(v);return;}
        if(n=="s3eKeyboardClearState"){keyState.fill(0);finish(0);return;}
        if(n=="s3ePointerGetInt"||n=="s3eKeyboardGetInt"||n=="s3ePointerUpdate"||n=="s3ePointerGetState"||n=="s3ePointerGetY"){finish(0);return;}
        if(n=="s3eSoundGetInt"){U value=0;if(r[0]==0)value=masterVolume;else if(r[0]==1||r[0]==2)value=soundRate;else if(r[0]==3)value=soundChannels;else if(r[0]==4){for(auto& c:channels)value+=c.playing;}else if(r[0]==5)value=1;else if(r[0]==6)value=256;finish(value);return;}
        if(n=="s3eSoundSetInt"){if(r[0]==0)masterVolume=std::min<U>(256,r[1]);finish(0);return;}
        if(n=="s3eSoundGetFreeChannel"){U free=0xffffffff;for(U ch=0;ch<soundChannels;++ch)if(!channels[ch].playing){free=ch;break;}finish(free);return;}
        if(n=="s3eSoundChannelRegister"||n=="s3eSoundChannelUnRegister"){if(r[0]>=soundChannels||r[1]>=4){finish(1);return;}auto& c=channels[r[0]];audioEvent(n.c_str(),r[0]);c.fn[r[1]]=n=="s3eSoundChannelRegister"?r[2]:0;c.ud[r[1]]=n=="s3eSoundChannelRegister"?r[3]:0;if(traceAudio)std::fprintf(stdout,"[Audio] %s ch=%u event=%u fn=%08x\n",n.c_str(),r[0],r[1],r[2]);finish(0);return;}
        if(n=="s3eSoundChannelSetInt"){if(r[0]>=soundChannels){finish(1);return;}auto& c=channels[r[0]];if(r[1]==1)c.rate=r[2];else if(r[1]==2)c.uservar=r[2];else if(r[1]==3)c.volume=std::min<U>(r[2],256);else if(r[1]==4)c.paused=r[2]!=0;else{finish(1);return;}finish(0);return;}
        if(n=="s3eSoundChannelGetInt"){U v=0;if(r[0]<soundChannels){auto& c=channels[r[0]];if(r[1]==0)v=c.playing;else if(r[1]==1)v=c.rate;else if(r[1]==2)v=c.uservar;else if(r[1]==3)v=c.volume;else if(r[1]==4)v=c.paused;}finish(v);return;}
        if(n=="s3eSoundChannelPlay"){if(r[0]>=soundChannels){finish(1);return;}auto& c=channels[r[0]];c.start=r[1];c.length=r[2];c.repeat=static_cast<int32_t>(r[3]);c.loop=static_cast<int32_t>(word(reg(Cpu::REG_SP)));c.suppressed=false;c.effectWave=effectGenerator(c.fn[1])?effectWave(c):0;
            // NFS short-effect generators: one audible voice per identical sample.
            // Still advance every decoder and deliver its normal end callback so
            // guest sound instances/channels are released, including muted duplicates.
            if(effectGenerator(c.fn[1])&&c.repeat==0){
                for(U k=0;k<soundChannels;++k){const auto& other=channels[k];
                    if(k!=r[0]&&other.playing&&!other.paused&&!other.suppressed&&other.fn[1]==c.fn[1]&&other.start==c.start&&other.length==c.length){c.suppressed=true;break;}
                }
            }
            c.pos=0;c.playing=true;c.paused=false;
            if(traceAudio&&c.suppressed)fprintf(stdout,"[AudioDedup] frame=%u ch=%u sample=%08x length=%u\n",frames,r[0],c.start,c.length);if(!audioStarted){audioStarted=true;audioStart=ms();}++soundStarts;audioEvent("play",r[0]);if(traceAudio)std::fprintf(stdout,"[Audio] Play ch=%u start=%08x samples=%u repeat=%d generator=%08x user=%08x lr=%08x\n",r[0],c.start,c.length,c.repeat,c.fn[1],c.ud[1],reg(Cpu::REG_LR));finish(0);return;}
        if(n=="s3eSoundChannelStop"||n=="s3eSoundChannelPause"||n=="s3eSoundChannelResume"){if(r[0]>=soundChannels){finish(1);return;}auto& c=channels[r[0]];audioEvent(n.c_str(),r[0]);if(n=="s3eSoundChannelStop")c.playing=c.paused=false;else c.paused=n=="s3eSoundChannelPause";finish(0);return;}
        // Compressed s3eAudio streams are still unavailable; PCM/IwSound uses the mixer above.
        if(n=="s3eAudioPlayFromBuffer"||n=="s3eAudioStop"||n=="s3eAudioPause"||n=="s3eAudioResume"){finish(1);return;}
        if(n=="s3eAudioIsPlaying"){finish(0);return;}
        if(n=="s3eInetHtons"){finish(((r[0]&255)<<8)|((r[0]>>8)&255));return;}
        if(n=="s3eFileAddUserFileSys"){std::array<U,11> t;rd(r[0],t.data(),44);fs.push_back(t);finish(0);return;}
        if(n=="s3eFileOpen"){++fileApiOpens;std::fprintf(stdout,"[S3E] Open %s\n",str(r[0]).c_str());open(r,done);return;}
        if(n=="s3eFileGetFileInt"){
            if(r[1]!=4){fatal("unsupported file-stat property "+std::to_string(r[1]));return;}
            U mode=alloc(4);wr(mode,"rb",3);std::string name=str(r[0]);
            open({r[0],mode,0,0},[this,mode,name](U h){heap.Free(mode);if(!h){set(Cpu::REG_R1,0xffffffffu);finish(0xffffffffu);return;}getSize(h,[this,h,name](U bytes){close(h,[this,name,bytes](U){std::fprintf(stdout,"[S3E] File size %s = %lu\n",name.c_str(),(unsigned long)bytes);set(Cpu::REG_R1,0);finish(bytes);});});});return;
        }
        if(n=="s3eFileCheckExists"){if(getenv("NFS_RESOURCE_TRACE"))fprintf(stderr,"[Exists] %s\n",str(r[0]).c_str());U m=alloc(4);const char mode[]="rb";wr(m,mode,3);open({r[0],m,0,0},[this,m](U h){heap.Free(m);if(!h){finish(0);return;}close(h,[this](U){finish(1);});});return;}
        if(n=="s3eFileRead"){read(r,done);return;}if(n=="s3eFileClose"){close(r[0],done);return;}if(n=="s3eFileSeek"){seek(r,done);return;}if(n=="s3eFileTell"){tell(r[0],done);return;}if(n=="s3eFileGetSize"){getSize(r[0],done);return;}
        if(n=="s3eFileEOF"){auto g=handles.find(r[0]);if(g!=handles.end())call(fs[g->second][2],{r[0]},done);else{auto f=files.find(r[0]);finish(f==files.end()||f->second.pos>=f->second.data.size());}return;}
        if(n=="s3eFileGetChar"){U p=alloc(1);read({p,1,1,r[0]},[this,p](U got){uint8_t b=0;if(got)rd(p,&b,1);heap.Free(p);finish(got?b:0xffffffffu);});return;}
        if(n=="s3eFileOpenFromMemory"){File f;f.data.resize(r[1]);rd(r[0],f.data.data(),f.data.size());f.backing=r[0];U h=alloc(64);files[h]=std::move(f);finish(h);return;}
        if(n=="s3eFileWrite"){auto f=files.find(r[3]);if(f==files.end()||!f->second.backing){fatal("unsupported file write");return;}File& q=f->second;size_t count=std::min<uint64_t>(uint64_t(r[1])*r[2],q.pos<q.data.size()?q.data.size()-q.pos:0);rd(r[0],q.data.data()+q.pos,count);wr(q.backing+q.pos,q.data.data()+q.pos,count);q.pos+=count;finish(r[1]?count/r[1]:0);return;}
        if(n=="s3eSecureStorageGet"){std::vector<char> d;secureError=SaveStore(saves).Read(d,r[1]);if(!secureError)wr(r[0],d.data(),d.size());finish(secureError?1:0);return;}
        if(n=="s3eSecureStorageGetError"){U error=secureError;secureError=0;finish(error);return;}
        if(n=="s3eSecureStoragePut"){std::vector<char>d(r[1]);rd(r[0],d.data(),d.size());if(failed)return;secureError=SaveStore(saves).Write(d)?0:1;finish(secureError?1:0);return;}
        if(n=="s3eCompressionDecompInit"){if(r[0]!=0){fatal("unsupported compression");return;}U h=alloc(16);auto d=std::make_shared<Decoder>();d->reader=r[1];d->user=r[2];d->request=alloc(8);d->buffer=alloc(4096);decoders[h]=d;finish(h);return;}
        if(n=="s3eCompressionDecompRead"){auto d=decoders.find(r[0]);if(d==decoders.end()){fatal("unknown decoder");return;}auto s=std::make_shared<DecompRead>();s->d=d->second;s->dst=r[1];s->countptr=r[2];s->need=word(r[2]);decompStep(s);return;}
        if(n=="s3eCompressionDecompFinal"){auto d=decoders.find(r[0]);if(d!=decoders.end()){heap.Free(d->second->buffer);heap.Free(d->second->request);decoders.erase(d);heap.Free(r[0]);}finish(0);return;}
        if(n=="s3eDebugTraceLine"){std::fprintf(stdout,"[Guest] %s %s\n",str(r[0]).c_str(),str(r[1]).c_str());finish(0);return;}
        if(n=="s3eDebugErrorShow"||n=="s3eDebugAssertShow"){fatal(n+": "+str(r[1]));return;}
        if(n=="s3eExtGet"){if(getenv("NFS_DIAG"))fprintf(stderr,"[Ext] %s out=%08x\n",str(r[0]).c_str(),r[1]);finish(1);return;}
        if(n=="s3eDeviceYield"){pollHost(true);
#ifdef __3DS__
            svcSleepThread(r[0]?1000000:0);
#endif
            pumpAudio([this](U){finish(0);});return;}
        static const std::set<std::string> noops={"s3eDeviceCheckPauseRequest","s3eDeviceBacklightOn","s3eSurfaceSetup","s3eSoundSetInt","s3eAudioSetInt","s3eSoundChannelRegister","s3eSoundChannelUnRegister","s3eAudioRegister","s3eVibraGetInt","s3eVibraSetInt","s3eVibraVibrate","s3eVibraStop"};
        if(noops.count(n)){finish(0);return;}fatal("unsupported import "+n+" caller "+std::to_string(reg(Cpu::REG_LR)));
    }
};
S3ERuntime::S3ERuntime(Cpu* c,const std::string&a,const std::string&s):p(new Impl(c,a,s)){}
S3ERuntime::~S3ERuntime()=default;
void S3ERuntime::Init(const std::vector<std::string>& i,const std::string& c,bool hardware){p->imports=i;p->parse(c);
    p->hardwareRenderer = hardware;
    if(p->hardwareRenderer){
        InstallProbeHost();
        auto& gl=GLES::Instance();
        gl.SetGuestAlloc([this](uint32_t size){return p->alloc(size);});
        gl.SetDisplay(320,240,320,240,400,240);
        gl.SetFrameCallback([this](const uint32_t* bgra,int w,int h){
#ifdef __3DS__
            if(!bgra){
                ++p->frames;
                p->measureFrame();
                if(p->present)p->present(p->frames,nullptr,p->desired);
                if(p->limit&&p->frames>=p->limit)p->quit=true;
                return;
            }
#endif
            if(!bgra||w<int(p->surfaceWidth)||h!=240)return;
            ++p->frames;
            p->measureFrame();
            const auto width=p->surfaceWidth;
            std::vector<uint16_t> rgb(width*h);
            for(unsigned i=0;i<rgb.size();++i){
                const auto px=bgra[(i/width)*w+(w-width)/2+i%width];
                rgb[i]=((px>>8)&0xf800)|((px>>5)&0x07e0)|((px>>3)&31);
            }
            if(p->present)p->present(p->frames,rgb.data(),p->desired);
            if(p->limit&&p->frames>=p->limit)p->quit=true;
        });
        p->config["gx/rasterisation"]="HW";
    }
    p->cpu->WriteTrampoline(TRAMPOLINE_BASE+i.size()*8,i.size());p->cpu->AddSvcHook([](Cpu* c,uint32_t,void* user){uint32_t pc=0;c->RegRead(Cpu::REG_PC,&pc);static_cast<Impl*>(user)->svc((pc-TRAMPOLINE_BASE-4)/8);},p.get());p->cpu->AddCodeHook([](Cpu*,uint64_t,uint32_t,void* user){static_cast<Impl*>(user)->softkeys();},p.get(),G(0x4a075a44),G(0x4a075a44));p->cpu->AddCodeHook([](Cpu*,uint64_t,uint32_t,void* user){static_cast<Impl*>(user)->guardEffectStart();},p.get(),G(0x4a0554e8),G(0x4a0554e8));
#ifdef __3DS__
    uint32_t bannerSignature = 0, cruiserCallSignature = 0;
    if (p->hardwareRenderer &&
        p->cpu->MemRead(G(0x4a080c08), &bannerSignature, sizeof(bannerSignature)) == 0 &&
        p->cpu->MemRead(G(0x4a083ed4), &cruiserCallSignature, sizeof(cruiserCallSignature)) == 0 &&
        bannerSignature == 0x000ab510 && cruiserCallSignature == 0xfe98f7fc) {
        p->cpu->AddCodeHook([](Cpu*,uint64_t,uint32_t,void* user){
            static_cast<Impl*>(user)->observeRaceBanner();
        }, p.get(), G(0x4a080c08), G(0x4a080c08));
    }
#endif
    // Guard the title-specific hooks against an unexpected prepared executable.
    uint32_t updateSignature = 0;
    uint32_t lodSignature = 0;
    p->cpu->MemRead(G(0x4a08404c), &updateSignature, sizeof(updateSignature));
    p->cpu->MemRead(G(0x4a05e312), &lodSignature, sizeof(lodSignature));
    if (updateSignature == 0x0005b5f3 && lodSignature == 0xd0292800) {
        p->cpu->AddCodeHook([](Cpu*,uint64_t,uint32_t,void* user){
            static_cast<Impl*>(user)->trackPlayerModel();
        }, p.get(), G(0x4a08404c), G(0x4a08404c));
        p->cpu->AddCodeHook([](Cpu*,uint64_t,uint32_t,void* user){
            static_cast<Impl*>(user)->keepPlayerModelDetailed();
        }, p.get(), G(0x4a05e312), G(0x4a05e312));
    }
}

void S3ERuntime::SetFrameCallback(std::function<void(U,const uint16_t*,Keys&)> f){p->present=std::move(f);}
void S3ERuntime::SetPollCallback(std::function<void(Keys&)> f){p->poll=std::move(f);}
void S3ERuntime::SetAudioCallback(std::function<void(const int16_t*,size_t,U)> f){p->audioOut=std::move(f);}
void S3ERuntime::SetChannelAudioCallback(std::function<void(U,const int16_t*,size_t,U)> f){p->audioStemOut=std::move(f);}
void S3ERuntime::RequestQuit(){p->quit=true;p->cpu->Stop();}
int S3ERuntime::Run(U e,U limit){p->limit=limit;int err=p->cpu->Start(e,0xDEADBEEF);p->reportPerformance();std::fprintf(stdout,"[S3E] End: error=%d failed=%d frame=%lu heap peak=%lu KB\n",err,p->failed,(unsigned long)p->frames,(unsigned long)(p->heap.PeakInUse()/1024));return err?err:p->failed?1:0;}
uint32_t S3ERuntime::Frames()const{return p->frames;}bool S3ERuntime::Failed()const{return p->failed;}

uint32_t S3ERuntime::SoftkeyPrompts()const{return p->prompts;}

unsigned S3ERuntime::SurfaceWidth() const {return p->surfaceWidth;}
void S3ERuntime::RequestDisplayToggle(){p->surfaceToggleRequested=true;}
