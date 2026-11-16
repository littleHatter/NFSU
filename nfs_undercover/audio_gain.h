#pragma once
#include <cstdint>
inline int16_t NfsDoubleVolume(int16_t sample){
    int32_t value=int32_t(sample)*2;
    return static_cast<int16_t>(value>32767?32767:value< -32768?-32768:value);
}
