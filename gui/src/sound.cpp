#include "sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include "console.hpp"
#include "i18n.hpp"

namespace fcg::sound {

namespace {

SDL_AudioStream* g_stream = nullptr;
bool g_tried              = false;  // the device was asked for since sounds were switched on
int g_volume              = 70;
std::array<std::vector<float>, 2> g_sounds;  // by Kind, made once

// A wooden piece put down: a click of noise for the contact, a short ring for the wood, a low thump for the board.
void knock(std::vector<float>& out, int rate, double at, double ring_hz, double thump_hz, double gain, uint32_t& seed) {
    const auto first = static_cast<std::size_t>(at * rate);
    double low       = 0.0;  // the noise softened: a one-pole low-pass filter
    for (std::size_t i = first; i < out.size(); ++i) {
        const double t     = static_cast<double>(i - first) / rate;
        seed               = seed * 1664525u + 1013904223u;
        const double noise = static_cast<double>(seed >> 8) / 8388608.0 - 1.0;
        low += 0.35 * (noise - low);
        const double ring  = std::sin(2 * std::numbers::pi * ring_hz * t) * std::exp(-t / 0.011);
        const double thump = std::sin(2 * std::numbers::pi * thump_hz * t) * std::exp(-t / 0.024);
        const double click = low * std::exp(-t / 0.0025);
        out[i] += static_cast<float>(gain * (0.5 * ring + 0.65 * thump + 0.6 * click));
    }
}

}  // namespace

std::vector<float> synthesize(Kind kind, int rate) {
    const bool capture = kind == Kind::Capture;
    std::vector<float> out(static_cast<std::size_t>(rate * (capture ? 0.17 : 0.1)), 0.0f);
    uint32_t seed = 0x2545F491u;
    if (capture) {
        // Heavier and lower, then the piece taken knocked aside.
        knock(out, rate, 0.0, 780.0, 130.0, 1.0, seed);
        knock(out, rate, 0.055, 1250.0, 210.0, 0.55, seed);
    } else {
        knock(out, rate, 0.0, 1050.0, 170.0, 1.0, seed);
    }
    // A millisecond to rise and five to fall, so that neither end clicks; a peak of 0.9.
    const std::size_t rise = static_cast<std::size_t>(rate / 1000), fall = static_cast<std::size_t>(rate / 200);
    for (std::size_t i = 0; i < rise && i < out.size(); ++i) out[i] *= static_cast<float>(i) / rise;
    for (std::size_t i = 0; i < fall && i < out.size(); ++i) out[out.size() - 1 - i] *= static_cast<float>(i) / fall;
    float peak = 0.0f;
    for (const float v : out) peak = std::max(peak, std::abs(v));
    if (peak > 0.0f) {
        for (auto& v : out) v *= 0.9f / peak;
    }
    return out;
}

void configure(bool on, int volume) {
    g_volume = std::clamp(volume, 0, 100);
    if (!on) {
        shutdown();
        g_tried = false;
        return;
    }
    if (g_stream || g_tried) return;
    g_tried = true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        Console::instance().add(trf("No move sounds: the sound could not be started ({}).", SDL_GetError()), true);
        return;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, RATE};
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!g_stream) {
        Console::instance().add(trf("No move sounds: no sound device ({}).", SDL_GetError()), true);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    SDL_ResumeAudioStreamDevice(g_stream);
    if (g_sounds[0].empty()) {
        g_sounds[0] = synthesize(Kind::Move);
        g_sounds[1] = synthesize(Kind::Capture);
    }
}

bool ready() { return g_stream != nullptr; }

void play(Kind kind) {
    if (!g_stream || g_volume == 0) return;
    // The loudness heard grows about as the square of the amplitude.
    const float gain = static_cast<float>(g_volume * g_volume) / 10000.0f;
    auto samples     = g_sounds[kind == Kind::Capture ? 1 : 0];
    for (auto& v : samples) v *= gain;
    SDL_ClearAudioStream(g_stream);
    SDL_PutAudioStreamData(g_stream, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
}

void shutdown() {
    if (!g_stream) return;
    SDL_DestroyAudioStream(g_stream);  // and the device it opened
    g_stream = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

}  // namespace fcg::sound
