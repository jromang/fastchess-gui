// fcg-board3d: the 3D board's prototype viewer, outside the GUI. Drag to turn round the board (right button: move),
// wheel to come closer, R to read the shaders again from the sources (gui/src/board3d/shaders).
//
//   fcg-board3d [--fen <FEN>] [--yaw r] [--pitch r] [--dist d] [--aperture a] [--studio n] [--exposure e]
//               [--varnish v] [--varnish-rough r] [--spp n] [--size WxH] [--shot out.png]
//
// --shot renders --spp samples in a hidden window, writes the PNG and quits.

#include "board3d/renderer.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

const std::string kDir = FCG_BOARD3D_DIR;

bool loadShaders(fcg::board3d::ShaderSources& s) {
    const std::string dir = kDir + "/gui/src/board3d/shaders/";
    s.trace   = readFile(dir + "trace.frag");
    s.prepare = readFile(dir + "prepare.frag");
    s.atrous  = readFile(dir + "atrous.frag");
    s.display = readFile(dir + "display.frag");
    return !s.trace.empty() && !s.prepare.empty() && !s.atrous.empty() && !s.display.empty();
}

// The camera kept inside the studio (trace.frag: 36 squares round, 30 high).
constexpr float kMaxDistance = 24.0f, kMaxPan = 5.0f;

// The preview's scale while the camera moves: a whole sample in about 16 ms, from the time of one at full size.
int previewScale(double fullPassMs) {
    if (fullPassMs <= 0) return 2;
    return std::clamp(int(std::ceil(std::sqrt(fullPassMs / 16.0))), 1, 6);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace fcg::board3d;
    std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    std::string shot;
    int width = 1280, height = 800, spp = 256;
    Camera camera;
    Lighting lighting;
    Finish finish;
    bool denoise = true;
    int orbit    = 0;
    float exposure = 0;  // the studio's own unless given
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string a = argv[i], v = argv[i + 1];
        if (a == "--fen") fen = v;
        else if (a == "--shot") shot = v;
        else if (a == "--spp") spp = std::stoi(v);
        else if (a == "--yaw") camera.yaw = std::stof(v);
        else if (a == "--pitch") camera.pitch = std::stof(v);
        else if (a == "--dist") camera.distance = std::stof(v);
        else if (a == "--aperture") camera.aperture = std::stof(v);
        else if (a == "--studio") lighting.studio = std::clamp(std::stoi(v), 0, int(studios().size()) - 1);
        else if (a == "--exposure") exposure = std::stof(v);
        else if (a == "--varnish") finish.varnish = std::stof(v);
        else if (a == "--varnish-rough") finish.varnishRough = std::stof(v);
        else if (a == "--bounces") lighting.bounces = std::stoi(v);
        else if (a == "--denoise") denoise = v != "0";
        else if (a == "--orbit") orbit = std::stoi(v);
        else if (a == "--size") std::sscanf(v.c_str(), "%dx%d", &width, &height);
        else if (a == "--target") std::sscanf(v.c_str(), "%f,%f,%f", &camera.target[0], &camera.target[1], &camera.target[2]);
    }
    const bool shooting = !shot.empty() || orbit > 0;
    lighting.exposure   = exposure > 0 ? exposure : studios()[std::size_t(lighting.studio)].exposure;
    camera.distance     = std::clamp(camera.distance, 3.0f, kMaxDistance);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | (shooting ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Window* window    = SDL_CreateWindow("fcg-board3d", width, height, flags);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    // Which GPU draws: the panel says so (with PRIME offload, the discrete one or not).
    std::string gpu = "?";
    using GetString = const unsigned char* (*)(unsigned);
    if (auto get = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString")))
        if (const unsigned char* r = get(0x1F01)) gpu = reinterpret_cast<const char*>(r);
    std::fprintf(stderr, "GPU: %s\n", gpu.c_str());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 330");

    ShaderSources shaders;
    std::string why;
    if (!loadShaders(shaders)) {
        std::fprintf(stderr, "shaders not found under %s\n", kDir.c_str());
        return 1;
    }
    const std::string pack = readFile(kDir + "/gui/assets/board3d/pieces.bin");
    Renderer renderer;
    const auto t0 = std::chrono::steady_clock::now();
    if (!renderer.init(shaders, reinterpret_cast<const unsigned char*>(pack.data()), pack.size(), why)) {
        std::fprintf(stderr, "board3d: %s\n", why.c_str());
        return 1;
    }
    const double initMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "init (decode, BVH, shaders) %.1f ms\n", initMs);
    renderer.setPieces(piecesFromFen(fen));
    renderer.setTargetSamples(spp);
    renderer.setDenoise(denoise);
    renderer.setFinish(finish);

    if (orbit > 0) {
        // The camera turning round the board, a preview each frame as while dragging it: the frame times.
        renderer.setLighting(lighting);
        renderer.setCamera(camera);
        while (renderer.samples() < 2) renderer.render(width, height, 50.0), renderer.finish();
        const double full = renderer.passMs();
        const int scale   = previewScale(full);
        std::vector<double> times;
        for (int i = 0; i < orbit; ++i) {
            camera.yaw += 0.02f;
            renderer.setCamera(camera);
            const auto t = std::chrono::steady_clock::now();
            renderer.render(width, height, 12.0, scale);
            renderer.finish();
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
        }
        std::sort(times.begin(), times.end());
        std::fprintf(stderr, "orbit %dx%d: full-size sample %.1f ms; preview 1/%d: frame median %.1f ms, p90 %.1f ms\n",
                     width, height, full, scale, times[times.size() / 2], times[times.size() * 9 / 10]);
        std::vector<std::uint8_t> rgba;
        renderer.readPixels(rgba);
        if (!shot.empty()) stbi_write_png(shot.c_str(), width, height, 4, rgba.data(), width * 4);
        renderer.shutdown();
        return 0;
    }

    if (shooting) {
        renderer.setCamera(camera);
        renderer.setLighting(lighting);
        const auto start = std::chrono::steady_clock::now();
        int last         = -1;
        while (!renderer.converged() || renderer.width() != width) {
            renderer.render(width, height, 50.0);
            renderer.finish();  // a hidden window paces nothing: without this the GPU's queue grows past its watchdog
            if (renderer.samples() != last && renderer.samples() % 16 == 0) {
                last = renderer.samples();
                std::fprintf(stderr, "\r%d/%d samples, %.1f ms per sample, %d bands", last, spp, renderer.passMs(),
                             renderer.bands());
            }
        }
        std::vector<std::uint8_t> rgba;
        renderer.readPixels(rgba);  // waits for the GPU
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        stbi_write_png(shot.c_str(), width, height, 4, rgba.data(), width * 4);
        std::fprintf(stderr, "\n%s: %d samples in %.2f s (%.1f ms per sample, last timed pass)\n", shot.c_str(),
                     renderer.samples(), s, renderer.passMs());
        // The radiance of the pixels: mean and spread, to see light levels and fireflies.
        std::vector<float> accum;
        renderer.readAccumulation(accum);
        std::vector<float> lum;
        double sum = 0;
        for (std::size_t i = 0; i < accum.size(); i += 4) {
            float n = std::max(accum[i + 3], 1.0f);
            float l = (0.2126f * accum[i] + 0.7152f * accum[i + 1] + 0.0722f * accum[i + 2]) / n;
            lum.push_back(l);
            sum += l;
        }
        std::sort(lum.begin(), lum.end());
        auto pct = [&](double p) { return lum[std::min(lum.size() - 1, std::size_t(p * double(lum.size())))]; };
        std::fprintf(stderr, "luminance mean %.4f  p10 %.4f  p50 %.4f  p90 %.4f  p99 %.4f  p99.9 %.4f  max %.4f\n",
                     sum / double(lum.size()), pct(0.1), pct(0.5), pct(0.9), pct(0.99), pct(0.999), lum.back());
        renderer.shutdown();
        return 0;
    }

    char fenBuffer[256];
    std::snprintf(fenBuffer, sizeof fenBuffer, "%s", fen.c_str());
    bool running      = true;
    Camera shown      = camera;
    double fullPassMs = 0;
    auto lastMove     = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    while (running) {
        SDL_Event e;
        ImGuiIO& io = ImGui::GetIO();
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (io.WantCaptureMouse) continue;
            if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
                camera.yaw -= e.motion.xrel * 0.006f;
                camera.pitch = std::clamp(camera.pitch + e.motion.yrel * 0.004f, 0.08f, 1.55f);
            } else if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_RMASK)) {
                float c = std::cos(camera.yaw), s = std::sin(camera.yaw), k = camera.distance * 0.0015f;
                camera.target[0] = std::clamp(camera.target[0] - (c * e.motion.xrel - s * e.motion.yrel) * k, -kMaxPan, kMaxPan);
                camera.target[2] = std::clamp(camera.target[2] - (-s * e.motion.xrel - c * e.motion.yrel) * k, -kMaxPan, kMaxPan);
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                camera.distance = std::clamp(camera.distance * std::pow(0.9f, e.wheel.y), 3.0f, kMaxDistance);
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_R && !io.WantCaptureKeyboard) {
                if (loadShaders(shaders) && !renderer.reloadShaders(shaders, why))
                    std::fprintf(stderr, "%s\n", why.c_str());
            }
        }
        renderer.setCamera(camera);
        renderer.setLighting(lighting);
        renderer.setFinish(finish);

        // While the camera moves, and a moment after, the preview; then the full image.
        const auto now = std::chrono::steady_clock::now();
        if (std::memcmp(&shown, &camera, sizeof camera) != 0) lastMove = now, shown = camera;
        const bool moving = now - lastMove < std::chrono::milliseconds(150);
        if (!moving && renderer.scale() == 1 && renderer.passMs() > 0) fullPassMs = renderer.passMs();
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        renderer.render(w, h, 12.0, moving ? previewScale(fullPassMs) : 1);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::GetBackgroundDrawList()->AddImage(ImTextureRef(ImTextureID(renderer.texture())), {0, 0},
                                                 io.DisplaySize, {0, 1}, {1, 0});
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
        ImGui::Begin("board3d", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted(gpu.c_str());
        ImGui::Text("%d / %d samples   %.1f ms per sample   %d bands", renderer.samples(), renderer.targetSamples(),
                    renderer.passMs(), renderer.bands());
        int target = renderer.targetSamples();
        if (ImGui::SliderInt("samples", &target, 16, 4096, "%d", ImGuiSliderFlags_Logarithmic))
            renderer.setTargetSamples(target);
        const auto all = studios();
        if (ImGui::BeginCombo("lighting", all[std::size_t(lighting.studio)].name)) {
            for (int i = 0; i < int(all.size()); ++i) {
                if (ImGui::Selectable(all[std::size_t(i)].name, i == lighting.studio)) {
                    lighting.studio   = i;
                    lighting.exposure = all[std::size_t(i)].exposure;
                }
                if (i == lighting.studio) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("exposure", &lighting.exposure, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("lights", &lighting.lights, 0.0f, 3.0f);
        ImGui::SliderFloat("ambient", &lighting.ambient, 0.0f, 4.0f);
        ImGui::SliderInt("bounces", &lighting.bounces, 1, 8);
        ImGui::SliderFloat("varnish", &finish.varnish, 0.0f, 1.0f);
        ImGui::SliderFloat("varnish rough", &finish.varnishRough, 0.02f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::Checkbox("denoise", &denoise)) renderer.setDenoise(denoise);
        ImGui::SliderFloat("aperture", &camera.aperture, 0.0f, 0.4f);
        ImGui::SliderFloat("fov", &camera.fovY, 0.2f, 1.2f);
        if (ImGui::InputText("FEN", fenBuffer, sizeof fenBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
            renderer.setPieces(piecesFromFen(fenBuffer));
        if (ImGui::Button("Save PNG")) {
            std::vector<std::uint8_t> rgba;
            renderer.readPixels(rgba);
            stbi_write_png("board3d.png", renderer.width(), renderer.height(), 4, rgba.data(), renderer.width() * 4);
        }
        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
