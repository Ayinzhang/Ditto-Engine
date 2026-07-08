#pragma once
#include <string>
#include <cstdint>




namespace AudioEngine
{
    using SoundHandle = uint32_t;   
    constexpr SoundHandle InvalidSound = 0;

    bool Init();
    void Shutdown();
    bool IsInitialized();

    
    
    
    SoundHandle Play(const std::string& filePath, float volume = 1.0f, bool loop = false);

    void Stop(SoundHandle handle);
    void SetVolume(SoundHandle handle, float volume);
    bool IsPlaying(SoundHandle handle);

    
    void StopAll();
}
