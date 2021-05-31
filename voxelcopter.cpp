#include <chrono>
#include <vector>
#include <map>
#include <iostream>
#include <thread>
#include "SDL2/SDL.h"
#include "SDL2/SDL_image.h"

using namespace std;

const char* audioFileName { "chop.wav" };
const char* textureFileName { "C23W.png" };
const char* heightmapFileName { "D21.png" };

namespace window {
    constexpr int width { 1920 };
    constexpr int height { 1080 };
}

constexpr int mapWidth { 1024 };

constexpr int painterThreadQuantity { 4 }; // 4 seems best for 6 CPU cores
vector<thread> painterWorkers;
thread inputHandler;
thread physics;

namespace camera {
    constexpr float straightView { window::height / 2.0f };
    constexpr float minHorizon { -straightView };
    constexpr float maxHorizon { 3.0f * straightView};
    float angle { 101.417f };         // Rotation left and right.
    float horizon { straightView - 30.0f};   // Rotation up and down.
    float distance { 4000.0f };       // How far can you see?
    constexpr float deltaFactor { 0.000001f };
    constexpr float heightScaleFactor { 50000.0f };
}

constexpr __v4sf gravity { 0, 0, -0.00001 };

namespace engine {
    constexpr float maxPower { 0.0000102 };
    constexpr float minPower { maxPower * 0.9f };
    constexpr float deltaPower { 0.0000000001 };
    float power { -gravity[2] + deltaPower * 5.0f};
}

namespace audio {
    Uint8* buffer { nullptr };
    Uint32 length { 0 };
}

namespace geo {
    int maxH { 0 };
    float pl[2] { 0 };
    float d[2] { 0 };
    float invZ { 0 };
}

__v4sf position { 1425.0f, -500.0f, 0.6f };
__v4sf drawingPosition { position };
__v4sf speed { 0 };

map<SDL_Keycode, bool> keyStates;

bool runApp { true };
SDL_Window* sdlWindow { nullptr };
SDL_Surface* windowSurface { nullptr };
Uint32* pixBuf { nullptr };
vector<int> hiddenY(window::width);

struct Point {
    float color[3];
    float height;
};

union PointVector {
    Point p;
    __v4sf v;
};

union Pixel {
    Uint32 uint;
    Uint8 color[4];
};

constexpr int skyColorShades { 200 };
vector<Pixel> skyColors(skyColorShades);
vector<Point> landscape;

struct SpinlockBarrier {
    atomic<int> prisoners { 0 };
    atomic<int> spinning { 0 };

    void wait() {
        ++spinning;
        ++prisoners;
        while (prisoners < painterThreadQuantity + 1 && runApp);
        if (--spinning == 0)
            prisoners = 0;
    }
};

SpinlockBarrier barrierA, barrierB;

Point getSurfaceColor(SDL_Surface* surf, int w, int h) {
    vector<Uint8> colorBuf(3);
    SDL_GetRGB(*((Uint8*)surf->pixels + h * surf->pitch + w * surf->format->BytesPerPixel),
               surf->format, &colorBuf[0], &colorBuf[1], &colorBuf[2]);
    return Point { { (float)colorBuf[0], (float)colorBuf[1], (float)colorBuf[2] } };
}

float getSurfaceHeight(SDL_Surface* surf, int w, int h) {
    const Point p { getSurfaceColor(surf, w, h) };
    return (p.color[0] + p.color[1] + p.color[2]) / 3.0f;
}

void initLandscape() {
    SDL_Surface* texture { IMG_Load(textureFileName) };
    SDL_Surface* heightmap { IMG_Load(heightmapFileName) };

    float maxHeight { 0.01f };
    for (int h = 0; h < heightmap->h; ++h)
        for (int w = 0; w < heightmap->w; ++w)
            if (getSurfaceHeight(heightmap, w, h) > maxHeight)
                maxHeight = getSurfaceHeight(heightmap, w, h);

    landscape.reserve(texture->w * texture->h);
    for (int h = 0; h < texture->h; ++h)
        for (int w = 0; w < texture->w; ++w) {
            Point p { getSurfaceColor(texture, w, h) };
            p.height = getSurfaceHeight(heightmap, w, h) / maxHeight;
            landscape.push_back(p);
        }

    SDL_FreeSurface(texture);
    SDL_FreeSurface(heightmap);
}

void getPoint(PointVector& p, const float* pos) {
    const auto getMapOffset = [](const int& x, const int& y) {
        return (y % mapWidth + mapWidth) % mapWidth * mapWidth + (x % mapWidth + mapWidth) % mapWidth;
    };

    const int intPos[2] { (int)floor(pos[0]), (int)floor(pos[1]) };
    const int nextIntPos[2] { intPos[0] + 1, intPos[1] + 1 };

    const float fract[2] { pos[0] - intPos[0], pos[1] - intPos[1] };
    const float negFract[2] { 1.0f - fract[0], 1.0f - fract[1] };

    const PointVector p1 { .p { landscape[getMapOffset(intPos[0], intPos[1])] } };
    const PointVector p2 { .p { landscape[getMapOffset(nextIntPos[0], intPos[1])] } };
    const PointVector p3 { .p { landscape[getMapOffset(intPos[0], nextIntPos[1])] } };
    const PointVector p4 { .p { landscape[getMapOffset(nextIntPos[0], nextIntPos[1])] } };

    p.v = p4.v * fract[0]    * fract[1]    +
          p3.v * negFract[0] * fract[1]    +
          p2.v * fract[0]    * negFract[1] +
          p1.v * negFract[0] * negFract[1];
}

void initSkyColors() {
    for (size_t i = 0; i < skyColorShades; ++i) {
        const double fadingRatio { (double)i / (double)skyColorShades };
        const Pixel lighterBlue { .color = { (Uint8)(232 - 52 * fadingRatio),
                                             (Uint8)(214 - 96 * fadingRatio),
                                             (Uint8)(181 - 123 * fadingRatio) } };
        skyColors.at(i) = lighterBlue;
    }
}

inline void drawLine(const Point& p, int x, int fromY, int toY) {
    const Pixel pixCol { .color = { (Uint8)p.color[2],
                                    (Uint8)p.color[1],
                                    (Uint8)p.color[0] } };
    for (auto i = fromY; i < toY; i++)
        pixBuf[x + i * windowSurface->w] = pixCol.uint;
}

void paintBlueSky() {
    for (int x = 0; x < window::width; ++x)
        for (int y = 0; y < hiddenY[x]; ++y) {
            int overHorizon { (int)camera::horizon - y };
            if (overHorizon >= skyColorShades)
                overHorizon = skyColorShades - 1;
            else if (overHorizon < 0)
                overHorizon = 0;
            pixBuf[x + y * windowSurface->w] = skyColors[overHorizon].uint;
        }
}

void drawPartialRow(const size_t fromX, const size_t toX) {
    while (runApp) {
        barrierA.wait();

        for (size_t i = fromX; i < toX; ++i) {
            if (geo::maxH >= hiddenY[i])
                continue;
            PointVector p;
            const float pos[] = { geo::pl[0] + geo::d[0] * i, geo::pl[1] + geo::d[1] * i };
            getPoint(p, pos);
            const int heightOnScreen = (drawingPosition[2] - p.p.height) * geo::invZ + camera::horizon;
            if (heightOnScreen < hiddenY[i]) {
                drawLine(p.p, i, heightOnScreen < 0 ? 0 : heightOnScreen, hiddenY[i]);
                hiddenY[i] = heightOnScreen;
            }
        }

        barrierB.wait();
    }
}

void calcGeo(const float& sinusAngle, const float& cosinusAngle, const float z) {
    const float cosinusAngleZ = cosinusAngle * z;
    const float sinusAngleZ = sinusAngle * z;
    geo::pl[0] = -cosinusAngleZ - sinusAngleZ;
    geo::pl[1] =  sinusAngleZ - cosinusAngleZ;
    geo::d[0] = (cosinusAngleZ - sinusAngleZ - geo::pl[0]) / (float)window::width;
    geo::d[1] = (-sinusAngleZ - cosinusAngleZ - geo::pl[1]) / (float)window::width;
    geo::pl[0] += drawingPosition[0];
    geo::pl[1] += drawingPosition[1];
    geo::invZ = camera::heightScaleFactor / z;
    geo::maxH = (drawingPosition[2] - 1.0f) * geo::invZ + camera::horizon;
}

void render() {
    const float sinusAngle { sinf(camera::angle) };
    const float cosinusAngle { cosf(camera::angle) };
    for (auto& y: hiddenY)
        y = window::height;

    float z { 1.0f };
    float row { 1.0f };
    while (z < camera::distance) {
        calcGeo(sinusAngle, cosinusAngle, z);
        barrierA.wait();
        barrierB.wait();
        z = 1.0f + row * row * row * camera::deltaFactor;
        ++row;
    }

    paintBlueSky();

    SDL_UpdateWindowSurface(sdlWindow);
    drawingPosition = position;
    camera::distance = 2000.0f + position[2] * 1000.0f;
}

void audioCallback(void* userdata, Uint8* audioBuffer, int audioBufferLen) {
    static int silenceBetweenChops { 512 };
    static int silencePlayed { 0 };
    static int chopPlayed { 0 };

    while (audioBufferLen) {
        int silenceToPlay { silenceBetweenChops - silencePlayed };
        if (silenceToPlay) {
            silenceToPlay = min(silenceToPlay, audioBufferLen);
            memset(audioBuffer, 0, silenceToPlay);
            silencePlayed += silenceToPlay;
            audioBuffer += silenceToPlay;
            audioBufferLen -= silenceToPlay;
            if (silencePlayed == silenceBetweenChops) {
                chopPlayed = 0;
                silencePlayed = silenceBetweenChops = 64 *
                    (int)(1.0f + 3000.0f * (1.0f - engine::power / engine::maxPower));
            }
        }

        int chopToPlay { (int)audio::length - chopPlayed };
        if (chopToPlay) {
            chopToPlay = min(chopToPlay, audioBufferLen);
            memcpy(audioBuffer, audio::buffer + chopPlayed, chopToPlay);
            audioBuffer += chopToPlay;
            audioBufferLen -= chopToPlay;
            chopPlayed += chopToPlay;
            if (chopPlayed == (int)audio::length)
                silencePlayed = 0;
        }
    }
}

void initSound() {
    SDL_AudioSpec wavSpec;
    if (!SDL_LoadWAV(audioFileName, &wavSpec, &audio::buffer, &audio::length)) {
        cerr << "Could not open audio file." << endl;
        return;
    }
    wavSpec.callback = audioCallback;
    if (0 == SDL_OpenAudio(&wavSpec, nullptr))
        SDL_PauseAudio(0);
}

void physicsThread() {
    while (runApp) {
        if (keyStates[SDLK_w]) {
            engine::power += engine::deltaPower;
            if (engine::power > engine::maxPower)
                engine::power = engine::maxPower;
        }
        if (keyStates[SDLK_s]) {
            engine::power -= engine::deltaPower;
            if (engine::power < engine::minPower)
                engine::power = engine::minPower;
        }
        if (keyStates[SDLK_ESCAPE])
            runApp = false;

        __v4sf sidePitch { 0 };
        if (keyStates[SDLK_a]) {
            sidePitch[0] -= sinf(camera::angle + M_PI_2) * engine::power;
            sidePitch[1] -= cosf(camera::angle + M_PI_2) * engine::power;
        }
        if (keyStates[SDLK_d]) {
            sidePitch[0] -= sinf(camera::angle - M_PI_2) * engine::power;
            sidePitch[1] -= cosf(camera::angle - M_PI_2) * engine::power;
        }

        const float frontPitch { (float)M_PI * (camera::horizon + camera::straightView * 9.0f) /
            (camera::straightView * 20.0f) };

        const __v4sf rotorAcceleration {
            -sinf(camera::angle) * engine::power * cosf(frontPitch) * 10.0f,
            -cosf(camera::angle) * engine::power * cosf(frontPitch) * 10.0f,
            sinf(frontPitch) * engine::power
        };

        speed *= 1.0f - (speed * speed * 4.0f); // Air resistance
        speed += rotorAcceleration + gravity + sidePitch;

        if (position[2] > 2.0f && speed[2] > 0.0f) // Height barrier
            speed[2] *= (5002.0f - position[2]) / 5000.0f;
        else if (position[2] < 1.0) { // Landing
            PointVector pv;
            const float pos[2] { position[0], position[1] };
            getPoint(pv, pos);
            if (position[2] <= pv.p.height + 0.011) {
                position[2] = pv.p.height + 0.0111;
                speed[0] = 0;
                speed[1] = 0;
                speed[2] = 0;
            }
        }

        position += speed;

        this_thread::sleep_for(1ms);
    }
}

void inputHandlerThread() {
    while (runApp) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                runApp = false;
            else if (event.type == SDL_MOUSEMOTION) {
                camera::angle -= (float)event.motion.xrel / 300.0f;
                camera::horizon += event.motion.yrel;
                if (camera::horizon < camera::minHorizon)
                    camera::horizon = camera::minHorizon;
                else if (camera::horizon > camera::maxHorizon)
                    camera::horizon = camera::maxHorizon;
            } else if (event.type == SDL_KEYDOWN)
                keyStates[event.key.keysym.sym] = true;
            else if (event.type == SDL_KEYUP)
                keyStates[event.key.keysym.sym] = false;
        }
    }
}

void cleanupAtExit() {
    runApp = false;
    inputHandler.join();
    physics.join();
    for (auto& worker: painterWorkers)
        worker.join();

    SDL_CloseAudio();
    SDL_FreeWAV(audio::buffer);
    SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
}

int main(int argc, char* argv[]) {
    atexit(cleanupAtExit);

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO);
    sdlWindow = SDL_CreateWindow("TerrainSpace", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 window::width, window::height, SDL_WINDOW_FULLSCREEN_DESKTOP);
    windowSurface = SDL_GetWindowSurface(sdlWindow);
    pixBuf = (Uint32*)windowSurface->pixels;

    initLandscape();
    initSkyColors();
    initSound();
    SDL_SetRelativeMouseMode(SDL_TRUE);

    inputHandler = thread(&inputHandlerThread);
    physics = thread(&physicsThread);
    constexpr size_t yPerThread { window::width / painterThreadQuantity };
    for (unsigned i = 0; i < painterThreadQuantity; ++i)
        painterWorkers.push_back(thread(drawPartialRow, i * yPerThread, (i + 1) * yPerThread));

    int fps { 0 };
    auto timeBeforeRender { chrono::system_clock::now() };
    while (runApp) {
        render();
        ++fps;
        const auto timeAfterRender { chrono::system_clock::now() };
        if (timeAfterRender - timeBeforeRender >= chrono::seconds(1)) {
            cout << "FPS: " << fps << endl;
            fps = 0;
            timeBeforeRender = timeAfterRender;
        }
    }
}
