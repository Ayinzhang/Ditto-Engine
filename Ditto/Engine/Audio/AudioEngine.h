#pragma once
#include <string>
#include <cstdint>

// Thin wrapper over miniaudio's high-level ma_engine API. One global engine;
// sounds are fire-and-managed handles. Header stays miniaudio-free so the
// (large) miniaudio implementation is confined to AudioEngine.cpp.
namespace AudioEngine
{
    using SoundHandle = uint32_t;   // 0 = invalid
    constexpr SoundHandle InvalidSound = 0;

    bool Init();
    void Shutdown();
    bool IsInitialized();

    // Start playing a sound file (wav/mp3/flac/ogg via stb_vorbis built into
    // miniaudio). Returns a handle for Stop/SetVolume, or InvalidSound on
    // failure. Finished one-shot sounds are reclaimed automatically.
    SoundHandle Play(const std::string& filePath, float volume = 1.0f, bool loop = false);

    void Stop(SoundHandle handle);
    void SetVolume(SoundHandle handle, float volume);
    bool IsPlaying(SoundHandle handle);

    // Stop everything (leaving Play mode / shutting down).
    void StopAll();
}
