#define MINIAUDIO_IMPLEMENTATION
#include "../../3rdParty/miniaudio.h"

#include "AudioEngine.h"
#include "../Core/Logger.h"
#include <unordered_map>
#include <memory>
#include <mutex>

namespace AudioEngine
{
    namespace
    {
        ma_engine g_engine;
        bool g_initialized = false;
        SoundHandle g_nextHandle = 1;
        std::unordered_map<SoundHandle, std::unique_ptr<ma_sound>> g_sounds;
        std::mutex g_mutex;

        // Drop sounds that finished playing (non-looping one-shots).
        void ReclaimFinished()
        {
            for (auto it = g_sounds.begin(); it != g_sounds.end();)
            {
                if (!ma_sound_is_playing(it->second.get()) && ma_sound_at_end(it->second.get()))
                {
                    ma_sound_uninit(it->second.get());
                    it = g_sounds.erase(it);
                }
                else ++it;
            }
        }
    }

    bool Init()
    {
        if (g_initialized) return true;

        ma_result result = ma_engine_init(nullptr, &g_engine);
        if (result != MA_SUCCESS)
        {
            DITTO_LOG_WARN_STREAM("[Audio] ma_engine_init failed: " << result);
            return false;
        }
        g_initialized = true;
        DITTO_LOG_INFO("[Audio] miniaudio engine initialized");
        return true;
    }

    void Shutdown()
    {
        if (!g_initialized) return;
        StopAll();
        ma_engine_uninit(&g_engine);
        g_initialized = false;
    }

    bool IsInitialized() { return g_initialized; }

    SoundHandle Play(const std::string& filePath, float volume, bool loop)
    {
        if (!g_initialized) return InvalidSound;

        std::lock_guard<std::mutex> lock(g_mutex);
        ReclaimFinished();

        auto sound = std::make_unique<ma_sound>();
        ma_result result = ma_sound_init_from_file(&g_engine, filePath.c_str(),
            MA_SOUND_FLAG_ASYNC, nullptr, nullptr, sound.get());
        if (result != MA_SUCCESS)
        {
            DITTO_LOG_WARN_STREAM("[Audio] Failed to load sound: " << filePath
                << " (error " << result << ")");
            return InvalidSound;
        }

        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(sound.get());

        SoundHandle handle = g_nextHandle++;
        if (g_nextHandle == InvalidSound) g_nextHandle = 1;   // wrap, skip 0
        g_sounds[handle] = std::move(sound);
        return handle;
    }

    void Stop(SoundHandle handle)
    {
        if (!g_initialized || handle == InvalidSound) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_sounds.find(handle);
        if (it == g_sounds.end()) return;
        ma_sound_stop(it->second.get());
        ma_sound_uninit(it->second.get());
        g_sounds.erase(it);
    }

    void SetVolume(SoundHandle handle, float volume)
    {
        if (!g_initialized || handle == InvalidSound) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_sounds.find(handle);
        if (it != g_sounds.end()) ma_sound_set_volume(it->second.get(), volume);
    }

    bool IsPlaying(SoundHandle handle)
    {
        if (!g_initialized || handle == InvalidSound) return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_sounds.find(handle);
        return it != g_sounds.end() && ma_sound_is_playing(it->second.get());
    }

    void StopAll()
    {
        if (!g_initialized) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& [handle, sound] : g_sounds)
        {
            ma_sound_stop(sound.get());
            ma_sound_uninit(sound.get());
        }
        g_sounds.clear();
    }
}
