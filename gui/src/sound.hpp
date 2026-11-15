#pragma once

// The move sounds: a short knock for a move, a heavier double knock for a capture, made here as samples (no sound
// file, so no license to track) and played through SDL's audio. Off by default: SDL's audio is started only when they
// are switched on, and stopped when they are switched off. Without a sound device, a line in the log says so and the
// program stays silent. One window: the module's own state, used from the window's thread.

#include <vector>

namespace fcg::sound {

enum class Kind { Move, Capture };

inline constexpr int RATE = 44100;  // samples per second

// The sound, mono, in [-1, 1], at `rate` samples per second; the same each time.
[[nodiscard]] std::vector<float> synthesize(Kind kind, int rate = RATE);

// Sounds on or off, at `volume` percent. Switched on, it opens the system's default sound device (once: after a
// failure it tries again only when switched off and on).
void configure(bool on, int volume);
// Whether a sound device is open: sounds on, and the device found.
[[nodiscard]] bool ready();
// Plays `kind`, cutting short the sound still playing; nothing when not ready().
void play(Kind kind);
// Closes the device and SDL's audio, before SDL_Quit().
void shutdown();

}  // namespace fcg::sound
