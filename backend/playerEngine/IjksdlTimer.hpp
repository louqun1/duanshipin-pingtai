#pragma once
#include <cstdint>
typedef struct SDL_SpeedSampler2
{
    int64_t sample_range;
    int64_t last_profile_tick;
    int64_t last_profile_duration;
    int64_t last_profile_quantity;
    int64_t last_profile_speed;
} SDL_SpeedSampler2;

class ijksdl_timer
{
public:
    ijksdl_timer(){

    }
};