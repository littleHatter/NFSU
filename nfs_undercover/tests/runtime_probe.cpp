// Exercise real ARM callbacks/import trampolines without loading menus or a race.
#include "loader.h"
#include "runtime_s3e.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
int main(int argc,char**argv){
 assert(argc==5);std::string error;auto cpu=Cpu::CreateUnicorn(&error);assert(cpu);
 S3EBinary bin;assert(LoadS3E(argv[1],bin));uint32_t base=std::stoul(argv[4],nullptr,16);
 assert(SetupSandbox(cpu.get(),bin,base,64u<<20));S3ERuntime rt(cpu.get(),argv[2],argv[3]);rt.Init(bin.imports,bin.config);
 const uint32_t stop=STACK_BASE+128,sp=STACK_BASE+STACK_SIZE-1024,obj=HEAP_BASE;
 auto set=[&](int r,uint32_t v){assert(cpu->RegWrite(r,&v)==0);};
 auto run=[&](uint32_t pc){set(Cpu::REG_SP,sp);set(Cpu::REG_LR,stop);assert(cpu->Start(pc,stop,1000000)==0);assert(!rt.Failed());};
 auto api=[&](const char* name,std::array<uint32_t,4> args){auto it=std::find(bin.imports.begin(),bin.imports.end(),name);assert(it!=bin.imports.end());for(int k=0;k<4;++k)set(k,args[k]);uint32_t zero=0;cpu->MemWrite(sp,&zero,4);run(TRAMPOLINE_BASE+8*(it-bin.imports.begin()));uint32_t result=0;cpu->RegRead(0,&result);return result;};
 struct Prompt {uint32_t a,b,mask;};
 for(auto test:{Prompt{0,0,0},Prompt{0x10,0,1},Prompt{0x80,0x100,3},Prompt{0x10,4,3},Prompt{1,0,4},Prompt{2,0,1}}){
  cpu->MemWrite(obj+0x714,&test.a,4);cpu->MemWrite(obj+0x710,&test.b,4);set(0,obj);run(G(0x4a075a45));assert(rt.SoftkeyPrompts()==test.mask);
 }
 std::ifstream f("tests/generator.bin",std::ios::binary);std::vector<uint8_t> generator((std::istreambuf_iterator<char>(f)),{});assert(generator.size()>8);
 cpu->MemWrite(G(0x4a04c31c),generator.data(),generator.size());
 cpu->MemWrite(G(0x4a0448d8),generator.data()+4,generator.size()-4);
 const uint32_t end=HEAP_BASE+64,counter=HEAP_BASE+112;
 const uint32_t endCode[]={0xe5912000,0xe2822001,0xe5812000,0xe3a00000,0xe12fff1e};cpu->MemWrite(end,endCode,sizeof(endCode));
 for(unsigned scenario=0;scenario<3;++scenario){
  uint32_t zero=0;cpu->MemWrite(counter,&zero,4);std::vector<int16_t> output;
  rt.SetAudioCallback([&](const int16_t*p,size_t n,uint32_t){output.insert(output.end(),p,p+n);});
  for(unsigned ch=0;ch<2;++ch){assert(api("s3eSoundChannelRegister",{ch,1,scenario==2?G(0x4a0448d8):G(0x4a04c31d),0})==0);assert(api("s3eSoundChannelRegister",{ch,0,end,counter})==0);assert(api("s3eSoundChannelPlay",{ch,HEAP_BASE+128+(scenario==1?ch*4:0),4,0})==0);}
  std::this_thread::sleep_for(std::chrono::milliseconds(30));api("s3eDeviceYield",{0,0,0,0});
  assert(output.size()>=512);for(unsigned i=0;i<512;++i)assert(output[i]==(scenario==0?100:200));
  uint32_t ended=0;cpu->MemRead(counter,&ended,4);assert(ended==2);
  for(unsigned ch=0;ch<2;++ch)assert(api("s3eSoundChannelGetInt",{ch,0,0,0})==0);
  std::cout<<"scenario "<<scenario<<": amplitude="<<output[0]<<" endCallbacks="<<ended<<" channels=free\n";
 }
 rt.SetAudioCallback([](const int16_t*,size_t,uint32_t){});
 // Observe the accepted path just after the guarded prologue. Stop there so
 // these requests cannot allocate real game voices in this minimal fixture.
 bool accepted=false;
 cpu->AddCodeHook([](Cpu*c,uint64_t,uint32_t,void* user){
  *static_cast<bool*>(user)=true;uint32_t lr=0;c->RegRead(Cpu::REG_LR,&lr);c->RegWrite(Cpu::REG_PC,&lr);
 },&accepted,G(0x4a0554f0),G(0x4a0554f0));
 const uint32_t wave=HEAP_BASE+192;
 cpu->MemWrite(obj+0x3c,&wave,4);
 auto request=[&](){accepted=false;set(0,obj);set(1,0);run(G(0x4a0554e9));return accepted;};
 assert(request()); // no active copy
 uint32_t zero=0;cpu->MemWrite(counter,&zero,4);
 api("s3eSoundChannelRegister",{0,1,G(0x4a04c31d),0});
 api("s3eSoundChannelRegister",{0,0,end,counter});
 api("s3eSoundChannelSetInt",{0,2,wave,0});
 api("s3eSoundChannelPlay",{0,HEAP_BASE+128,4,0});
 for(unsigned k=0;k<8;++k)assert(!request()); // no allocation or decoder replacement
 uint32_t different=wave+4;cpu->MemWrite(obj+0x3c,&different,4);assert(request());
 cpu->MemWrite(obj+0x3c,&wave,4);
 api("s3eSoundChannelPause",{0,0,0,0});assert(!request());
 api("s3eSoundChannelResume",{0,0,0,0});
 std::this_thread::sleep_for(std::chrono::milliseconds(30));api("s3eDeviceYield",{0,0,0,0});
 uint32_t ended=0;cpu->MemRead(counter,&ended,4);assert(ended==1);
 assert(api("s3eSoundChannelGetInt",{0,0,0,0})==0);assert(request());
 std::cout<<"PASS 8 retriggers blocked before allocation; distinct wave accepted; paused voice protected; completion frees voice and permits next request\n";
 // Regression from hardware captures: PCM impacts use 0xA1E2B and do not
 // update USER_VAR. Run the real PCM generator with both empty and stale values.
 const uint32_t fixture=HEAP_BASE+HEAP_SIZE-8192,owner=fixture,voice=fixture+256,decoder=fixture+512,pcm=fixture+1024;
 const uint16_t thumbToArm[]={0x4778,0x46c0};
 cpu->MemWrite(G(0x4a024338),thumbToArm,4);cpu->MemWrite(G(0x4a02433c),endCode,sizeof(endCode));
 cpu->MemWrite(owner+0x3c,&wave,4);cpu->MemWrite(voice+0xc,&owner,4);
 std::vector<int16_t> pcmSamples(1760);for(unsigned k=0;k<pcmSamples.size();++k)pcmSamples[k]=100+k%200;
 cpu->MemWrite(pcm,pcmSamples.data(),pcmSamples.size()*2);
 for(uint32_t stale:{0u,0xdeadbeefu}){
  uint32_t clean[4]{};cpu->MemWrite(decoder,clean,sizeof(clean));cpu->MemWrite(voice,&zero,4);
  api("s3eSoundChannelSetInt",{0,2,stale,0});api("s3eSoundChannelSetInt",{0,1,8000,0});
  api("s3eSoundChannelRegister",{0,1,G(0x4a0a1e2b),decoder});
  api("s3eSoundChannelRegister",{0,0,G(0x4a024339),voice});
  api("s3eSoundChannelPlay",{0,pcm,1760,0});
  unsigned blocked=0;
  for(unsigned k=0;k<32&&api("s3eSoundChannelGetInt",{0,0,0,0});++k){
   uint32_t before=0,after=0;cpu->MemRead(decoder,&before,4);assert(!request());cpu->MemRead(decoder,&after,4);assert(before==after);++blocked;
   std::this_thread::sleep_for(std::chrono::milliseconds(34));api("s3eDeviceYield",{0,0,0,0});
  }
  uint32_t completed=0;cpu->MemRead(voice,&completed,4);assert(completed==1&&blocked>=2);
  assert(api("s3eSoundChannelGetInt",{0,0,0,0})==0);assert(request());
  std::cout<<"PASS real PCM 1760@8000: USER_VAR="<<std::hex<<stale<<std::dec<<" blocked="<<blocked<<" cursor preserved; completion=1; next request accepted\n";
 }
 std::cout<<"PASS prompts, duplicate effects, distinct effects, engine exemption; base="<<std::hex<<base<<"\n";
}
