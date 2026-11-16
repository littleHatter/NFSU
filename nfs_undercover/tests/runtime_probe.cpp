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
 for(auto test:{Prompt{0,0,0},Prompt{0x10,0,1},Prompt{0x80,0x100,3},Prompt{0x10,4,3},Prompt{1,0,4},Prompt{2,0,0}}){
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
 std::cout<<"PASS prompts, duplicate effects, distinct effects, engine exemption; base="<<std::hex<<base<<"\n";
}
