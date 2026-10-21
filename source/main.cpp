#include <SDL.h>

#include "app/app.h"
#include "app/downloads.h"
#include "net/async.h"
#include "net/http.h"
#include "platform/platform.h"
#include "ui/gfx.h"
#include "ui/images.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "util/log.h"

#ifndef __SWITCH__
#include <unistd.h>

#include <climits>
#include <fstream>
#include <sstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// CI screenshots: `browsenx --script tests/shots.txt out/` drives the real app offscreen.
// Commands: open <url> | wait | shot <name> | press <A|B|X|Y|PLUS|MINUS|UP|DOWN> |
//           point <css selector> | type <text> | scroll <px> | waitdl | expect <file that must exist>
static int RunScript(App& app, SDL_Surface* surface, const char* scriptPath, const std::string& outDir) {
    std::ifstream f(scriptPath);
    if (!f) {
        fprintf(stderr, "can't read %s\n", scriptPath);
        return 1;
    }
    char cwd[PATH_MAX];
    const std::string here = getcwd(cwd, sizeof cwd) ? cwd : ".";
    auto frame = [&](uint32_t buttons) {
        Platform::Input in;
        in.held = in.down = buttons;
        app.Frame(in, 1.0f / 60);
        SDL_Delay(4);
    };
    int failures = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string cmd, arg;
        ss >> cmd;
        std::getline(ss >> std::ws, arg);
        for (size_t p; (p = arg.find("$PWD")) != std::string::npos;) arg.replace(p, 4, here);
        printf("script: %s %s\n", cmd.c_str(), arg.c_str());
        if (cmd == "open") {
            app.Navigate(arg);
            frame(0);
        } else if (cmd == "wait") {
            const uint32_t start = SDL_GetTicks();
            do frame(0);
            while ((app.Busy() || SDL_GetTicks() - start < 300) && SDL_GetTicks() - start < 25000);
            for (int i = 0; i < 10; i++) frame(0);
        } else if (cmd == "waitdl") {
            const uint32_t start = SDL_GetTicks();
            do frame(0);
            while (Downloads::AnyRunning() && SDL_GetTicks() - start < 60000);
            for (int i = 0; i < 10; i++) frame(0);
        } else if (cmd == "shot") {
            app.Draw();
            SDL_RenderPresent(Gfx::Renderer());
            std::string path = outDir + "/" + arg + ".png";
            stbi_write_png(path.c_str(), surface->w, surface->h, 4, surface->pixels, surface->pitch);
        } else if (cmd == "press") {
            static const std::pair<const char*, uint32_t> kNames[] = {
                {"A", Platform::BTN_A},       {"B", Platform::BTN_B},         {"X", Platform::BTN_X},
                {"Y", Platform::BTN_Y},       {"PLUS", Platform::BTN_PLUS},   {"MINUS", Platform::BTN_MINUS},
                {"UP", Platform::BTN_UP},     {"DOWN", Platform::BTN_DOWN},   {"L", Platform::BTN_L},
                {"R", Platform::BTN_R},
            };
            for (auto& k : kNames)
                if (arg == k.first) frame(k.second);
            frame(0);
        } else if (cmd == "point") {
            if (!app.PointAt(arg)) {
                printf("script: FAIL nothing matches %s\n", arg.c_str());
                failures++;
            }
            frame(0);
        } else if (cmd == "type") {
            Platform::ScriptKeyboard(arg);
        } else if (cmd == "scroll") {
            app.page.ScrollBy((float)atof(arg.c_str()));
            frame(0);
        } else if (cmd == "expect") {
            std::ifstream e(arg, std::ios::binary | std::ios::ate);
            if (!e || e.tellg() <= 0) {
                printf("script: FAIL %s is missing or empty\n", arg.c_str());
                failures++;
            }
        }
    }
    return failures ? 1 : 0;
}
#endif

int main(int argc, char** argv) {
    Platform::Init();
    Log::Init(Platform::DataDir() + "log.txt");
    LOG("BrowseNX starting (%s)", Platform::AppletMode() ? "applet" : "application");
    Http::GlobalInit();
    Async::Start();
    InitTheme();

#ifndef __SWITCH__
    if (argc >= 4 && std::string(argv[1]) == "--script") {
        SDL_Init(0);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, Gfx::W, Gfx::H, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* ren = SDL_CreateSoftwareRenderer(surface);
        Gfx::Init(ren);
        Text::Init();
        Images::Init(Platform::DataDir() + "cache/");
        App app;
        app.Init();
        int rc = RunScript(app, surface, argv[2], argv[3]);
        Http::AbortAll();
        app.Shutdown();
        Async::Stop();
        Images::Shutdown();
        Text::Shutdown();
        SDL_DestroyRenderer(ren);
        SDL_FreeSurface(surface);
        SDL_Quit();
        Http::GlobalCleanup();
        return rc;
    }
#else
    (void)argc;
    (void)argv;
#endif

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* win = SDL_CreateWindow("BrowseNX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, Gfx::W, Gfx::H, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!win || !ren) {
        LOG("SDL setup failed: %s", SDL_GetError());
        return 1;
    }
    Gfx::Init(ren);
    if (!Text::Init()) LOG("fonts missing");
    Images::Init(Platform::DataDir() + "cache/");

    App app;
    app.Init();
    uint64_t last = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();
    while (Platform::Running() && !app.quit) {
        Platform::Input in = Platform::PollInput();
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)((now - last) / freq);
        last = now;
        if (app.Frame(in, dt)) SDL_RenderPresent(ren);
        else SDL_Delay(16);
    }
    LOG("BrowseNX exiting");
    Http::AbortAll();
    app.Shutdown();
    Async::Stop();
    Images::Shutdown();
    Text::Shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    Http::GlobalCleanup();
    Platform::Shutdown();
    return 0;
}
