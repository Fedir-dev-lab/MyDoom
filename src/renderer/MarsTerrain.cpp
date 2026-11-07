#include "MarsTerrain.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include "renderer/Framebuffer.h"

namespace {
// Covers the 256 m landing basin plus the long sight lines visible from a
// mountain top. One metre samples keep player grounding exact.
constexpr int kResolution = 640;
constexpr int kVertexCount = kResolution + 1;
constexpr float kWorldMin = -320.0f;
constexpr float kWorldSize = 640.0f;

std::once_flag g_initializeOnce;
std::vector<float> g_heights;
std::vector<uint32_t> g_colors;

float Hash(float x, float z) {
    const float value = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f;
    return value - std::floor(value);
}

float Noise(float x, float z) {
    const float ix = std::floor(x), iz = std::floor(z);
    const float fx = x - ix, fz = z - iz;
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sz = fz * fz * (3.0f - 2.0f * fz);
    const float a = Hash(ix, iz), b = Hash(ix + 1.0f, iz);
    const float c = Hash(ix, iz + 1.0f), d = Hash(ix + 1.0f, iz + 1.0f);
    return (a + (b - a) * sx) * (1.0f - sz) + (c + (d - c) * sx) * sz;
}

float Mountain(float x, float z, float centerX, float centerZ,
               float radius, float peakHeight) {
    const float dx = x - centerX;
    const float dz = z - centerZ;
    const float distance = std::sqrt(dx * dx + dz * dz);
    const float t = std::clamp(1.0f - distance / radius, 0.0f, 1.0f);
    // A broad base plus a steeper top reads as a mountain rather than a dome.
    return peakHeight * t * t * (0.55f + 0.45f * t);
}

float ProceduralHeight(float x, float z) {
    const float broad = (Noise(x * 0.018f, z * 0.018f) - 0.5f) * 10.0f;
    const float dunes = (Noise(x * 0.075f + 19.0f, z * 0.075f - 8.0f) - 0.5f) * 1.7f;
    const float ridge = std::sin(x * 0.035f + std::sin(z * 0.018f) * 2.0f) * 1.1f;
    // Ranges around the landing basin replace the former box-shaped sector
    // walls. Peaks stay outside the pod area but remain visible on the skyline.
    const float mountains =
        Mountain(x, z, -102.0f,  72.0f, 58.0f, 42.0f) +
        Mountain(x, z,   98.0f,  86.0f, 64.0f, 48.0f) +
        Mountain(x, z,  -84.0f, -92.0f, 54.0f, 34.0f) +
        Mountain(x, z,   86.0f, -96.0f, 60.0f, 38.0f) +
        Mountain(x, z,    8.0f, 146.0f, 90.0f, 54.0f);
    return broad + dunes + ridge + mountains;
}

size_t HeightIndex(int x, int z) {
    return static_cast<size_t>(z) * kVertexCount + x;
}

size_t ColorIndex(int x, int z) {
    return static_cast<size_t>(z) * kResolution + x;
}

float GridCoordinate(float world) {
    return std::clamp(world - kWorldMin, 0.0f, kWorldSize);
}
}

void MarsTerrain::Initialize() {
    std::call_once(g_initializeOnce, [] {
        g_heights.resize(static_cast<size_t>(kVertexCount) * kVertexCount);
        g_colors.resize(static_cast<size_t>(kResolution) * kResolution);

        for (int z = 0; z < kVertexCount; ++z) {
            for (int x = 0; x < kVertexCount; ++x) {
                g_heights[HeightIndex(x, z)] = ProceduralHeight(
                    kWorldMin + static_cast<float>(x),
                    kWorldMin + static_cast<float>(z));
            }
        }

        for (int z = 0; z < kResolution; ++z) {
            for (int x = 0; x < kResolution; ++x) {
                const float height = g_heights[HeightIndex(x, z)];
                const float grain = Noise((kWorldMin + x) * 0.24f,
                                          (kWorldMin + z) * 0.24f) * 16.0f;
                const float shade = std::clamp(height * 1.2f, -12.0f, 12.0f);
                const float sandR = 108.0f + grain + shade;
                const float sandG = 48.0f + grain * 0.34f + shade * 0.35f;
                const float sandB = 31.0f + grain * 0.16f + shade * 0.2f;
                // Mountain peaks use a separate basalt-like palette instead
                // of merely darkening the same sand colour.
                const float rock = std::clamp((height - 9.0f) / 17.0f, 0.0f, 1.0f);
                const float rockVariation = grain * 0.28f;
                const int r = static_cast<int>(sandR + (68.0f + rockVariation - sandR) * rock);
                const int g = static_cast<int>(sandG + (55.0f + rockVariation - sandG) * rock);
                const int b = static_cast<int>(sandB + (49.0f + rockVariation - sandB) * rock);
                g_colors[ColorIndex(x, z)] = Framebuffer::MakeColor(
                    static_cast<uint8_t>(std::clamp(r, 20, 180)),
                    static_cast<uint8_t>(std::clamp(g, 12, 100)),
                    static_cast<uint8_t>(std::clamp(b, 8, 75)));
            }
        }
    });
}

float MarsTerrain::HeightAt(float x, float z, int lod) {
    Initialize();
    const int gridStep = 1 << std::clamp(lod, 0, 2);
    const float gridX = GridCoordinate(x);
    const float gridZ = GridCoordinate(z);
    const int x0 = std::min(kResolution - gridStep,
                            static_cast<int>(gridX / gridStep) * gridStep);
    const int z0 = std::min(kResolution - gridStep,
                            static_cast<int>(gridZ / gridStep) * gridStep);
    const float tx = (gridX - x0) / gridStep;
    const float tz = (gridZ - z0) / gridStep;
    const float a = g_heights[HeightIndex(x0, z0)];
    const float b = g_heights[HeightIndex(x0 + gridStep, z0)];
    const float c = g_heights[HeightIndex(x0, z0 + gridStep)];
    const float d = g_heights[HeightIndex(x0 + gridStep, z0 + gridStep)];
    return (a + (b - a) * tx) * (1.0f - tz) + (c + (d - c) * tx) * tz;
}

uint32_t MarsTerrain::ColorAt(float x, float z) {
    Initialize();
    const int gridX = std::clamp(static_cast<int>(GridCoordinate(x)), 0, kResolution - 1);
    const int gridZ = std::clamp(static_cast<int>(GridCoordinate(z)), 0, kResolution - 1);
    return g_colors[ColorIndex(gridX, gridZ)];
}
