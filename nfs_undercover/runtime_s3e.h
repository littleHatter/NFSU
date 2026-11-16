#pragma once
#include "cpu.h"
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <functional>
class S3ERuntime {
public:
    using Keys=std::array<bool,256>;
    S3ERuntime(Cpu* cpu,const std::string& assets,const std::string& saves);
    ~S3ERuntime();
    void Init(const std::vector<std::string>& imports,const std::string& config);
    void SetFrameCallback(std::function<void(uint32_t,const uint16_t*,Keys&)> fn);
    void SetPollCallback(std::function<void(Keys&)> fn);
    void SetAudioCallback(std::function<void(const int16_t*,size_t,uint32_t)> fn);
    void SetChannelAudioCallback(std::function<void(uint32_t,const int16_t*,size_t,uint32_t)> fn);
    void RequestQuit();
    int Run(uint32_t entry,uint32_t maxFrames=0);
    uint32_t Frames() const;
    bool Failed() const;
    uint32_t SoftkeyPrompts() const;
private:
    struct Impl;std::unique_ptr<Impl> p;
};
