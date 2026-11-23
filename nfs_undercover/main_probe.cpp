#include "loader.h"
#include "runtime_s3e.h"
#include "desktop_audio_sink.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
int main(int argc,char**argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 std::string assets=argc>1?argv[1]:"game",error;
 auto cpu=Cpu::CreateUnicorn(&error);if(!cpu){fprintf(stderr,"%s\n",error.c_str());return 2;}
 S3EBinary bin;if(!LoadS3E(assets+"/nfsuc.xe3u",bin)||!SetupSandbox(cpu.get(),bin,STOCK_BASE,64u<<20))return 2;
 for(const auto& p:bin.pointer_fixups){uint32_t actual=0;cpu->MemRead(G(bin.base_addr+p.offset),&actual,4);if(actual!=TRAMPOLINE_BASE+p.func_idx*8){fprintf(stderr,"Type-4 pointer verification failed\n");return 2;}}
 printf("[Probe] Type-4 function pointers patched: %zu\n",bin.pointer_fixups.size());
 cpu->AddCodeHook([](Cpu*c,uint64_t a,uint32_t,void*){if(!std::getenv("NFS_DIAG"))return;uint32_t r0=0,r1=0,lr=0;c->RegRead(0,&r0);c->RegRead(1,&r1);c->RegRead(14,&lr);fprintf(stderr,"[MemoryRoute] pc=%08x r0=%08x r1=%08x lr=%08x\n",(uint32_t)a,r0,r1,lr);},nullptr,0x4a02b114,0x4a02b114);
 cpu->AddCodeHook([](Cpu*c,uint64_t a,uint32_t,void*){if(!std::getenv("NFS_DIAG"))return;uint16_t op=0;c->MemRead(a,&op,2);fprintf(stderr,"[NullWindow] %08x op=%04x ",(uint32_t)a,op);for(int i=0;i<8;++i){uint32_t v=0;c->RegRead(i,&v);fprintf(stderr,"r%d=%08x ",i,v);}fprintf(stderr,"\n");},nullptr,0x4a050c50,0x4a050c6e);
 if(std::getenv("NFS_RESOURCE_TRACE"))cpu->AddCodeHook([](Cpu*c,uint64_t,uint32_t,void*){static unsigned n=0;++n;if(n>25&&n%1000)return;uint32_t r[17]{};for(int i=0;i<17;++i)c->RegRead(i,r+i);fprintf(stderr,"[ResourceSearch] n=%u r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x lr=%08x\n",n,r[0],r[1],r[2],r[3],r[13],r[14]);uint32_t obj[12]{};if(!c->MemRead(r[0],obj,sizeof(obj))){fprintf(stderr,"[Group]");for(auto v:obj)fprintf(stderr," %08x",v);fprintf(stderr,"\n");uint32_t deps[4]{};if(obj[9]&&obj[9]<=4&&!c->MemRead(obj[8],deps,obj[9]*4)){fprintf(stderr,"[Dependencies]");for(unsigned i=0;i<obj[9];++i)fprintf(stderr," %08x",deps[i]);fprintf(stderr,"\n");}}},nullptr,0x4a095fc8,0x4a095fc8);
 uint32_t frameLimit=argc>2?std::stoul(argv[2]):2;
 std::string output=argc>3?argv[3]:"frames",saveDir=argc>4?argv[4]:"probe_user";
 std::filesystem::create_directories(output);
 struct Input {uint32_t begin,end,key;};std::vector<Input> input;
 if(argc>5){std::ifstream f(argv[5]);Input e;while(f>>e.begin>>e.end>>e.key){if(e.key>=256||e.end<e.begin)return 2;input.push_back(e);}}
 S3ERuntime rt(cpu.get(),assets,saveDir);rt.Init(bin.imports,bin.config,std::getenv("NFS_HW_PROBE")!=nullptr);
 DesktopAudioSink audioSink;
 std::ofstream pcm(output+"/audio_s16le_22050.raw",std::ios::binary);
 rt.SetAudioCallback([&](const int16_t* samples,size_t count,uint32_t rate){audioSink.Push(samples,count,rate);pcm.write(reinterpret_cast<const char*>(samples),count*2);});
 std::ofstream musicStem,engineStem;
 if(std::getenv("NFS_AUDIO_STEMS")){musicStem.open(output+"/music_channel_00_s16le_22050.raw",std::ios::binary);engineStem.open(output+"/engine_channel_15_s16le_22050.raw",std::ios::binary);rt.SetChannelAudioCallback([&](uint32_t ch,const int16_t* data,size_t count,uint32_t){if(ch==0)musicStem.write(reinterpret_cast<const char*>(data),count*2);if(ch==15)engineStem.write(reinterpret_cast<const char*>(data),count*2);});}
 rt.SetFrameCallback([&](uint32_t n,const uint16_t* data,S3ERuntime::Keys& keys){
 keys.fill(false);for(const auto& e:input)if(n>=e.begin&&n<e.end)keys[e.key]=true;
 if(const char* path=std::getenv("NFS_LIVE_KEYS")){std::ifstream f(path);unsigned key;while(f>>key)if(key<256)keys[key]=true;}
 static S3ERuntime::Keys previous{};for(unsigned key=0;key<256;++key)if(keys[key]!=previous[key]){fprintf(stdout,"[Input] frame=%u key=%u down=%u\n",n,key,keys[key]?1:0);previous[key]=keys[key];}
 auto write=[&](const std::string& path){FILE*f=fopen(path.c_str(),"wb");if(!f)return;const unsigned width = rt.SurfaceWidth();fprintf(f,"P6\n%u 240\n255\n",width);for(unsigned i=0;i<width*240;++i){auto p=data[i];unsigned char c[3]={static_cast<unsigned char>((p>>11)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};fwrite(c,1,3,f);}fclose(f);};
 if(std::getenv("NFS_TEST_LIVE") && (n==2440||n==2600))rt.RequestDisplayToggle();
 write(output+"/latest.ppm");if(n<=3||n%30==0||n==2440||n==2441||n==2600||n==2601||n==frameLimit){char name[64];snprintf(name,sizeof(name),"/frame_%06u.ppm",n);write(output+name);}
 });
 int status=rt.Run(G(bin.entry_point),frameLimit);
 if(status&&!rt.Failed()){uint32_t pc=0,lr=0,sp=0;cpu->RegRead(Cpu::REG_PC,&pc);cpu->RegRead(Cpu::REG_LR,&lr);cpu->RegRead(Cpu::REG_SP,&sp);fprintf(stderr,"[Probe] CPU error %s pc=%08x lr=%08x sp=%08x\n",cpu->ErrorText(status),pc,lr,sp);for(int i=0;i<13;++i){uint32_t v=0;cpu->RegRead(i,&v);fprintf(stderr,"r%d=%08x ",i,v);}fprintf(stderr,"\n");}
 return status;
}
