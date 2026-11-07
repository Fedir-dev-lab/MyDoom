#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/Vector3.h"

// PointLight with precomputed radiusSq so the hot-path never divides or
// multiplies radius at runtime.
struct PointLight {
    Vector3 position;
    float   radius    = 8.0f;
    float   intensity = 1.0f;
    float   radiusSq  = 64.0f; // radius * radius — precomputed at construction

    PointLight() = default;
    PointLight(Vector3 pos, float r, float i)
        : position(pos), radius(r), intensity(i), radiusSq(r * r) {}
};

// ---------------------------------------------------------------------------
// DirectionalLight — infinite-distance light source (the sun).
// direction: unit vector pointing FROM the light TO the world (like sunlight).
//   computed from sunWorldAngle and sunElevation in main.cpp.
// intensity: 0 = night, 1 = noon.
// ---------------------------------------------------------------------------
struct DirectionalLight {
    float dirX = 0.0f; // horizontal X component of light direction
    float dirZ = 1.0f; // horizontal Z component of light direction
    float dirY = -1.0f; // vertical — negative = pointing downward (sun above)
    float intensity = 0.0f;

    // Build from spherical coordinates: worldAngle = horizontal yaw of the sun,
    // elevation = angle above horizon in radians.
    static DirectionalLight FromAngles(float worldAngle, float elevation) {
        DirectionalLight d;
        // Direction FROM sun TO ground: sun is at (sin(wa), sin(el), cos(wa)),
        // so light direction pointing down is the negation.
        d.dirX      = -std::sin(worldAngle) * std::cos(elevation);
        d.dirZ      = -std::cos(worldAngle) * std::cos(elevation);
        d.dirY      = -std::sin(elevation); // negative = pointing down
        d.intensity = std::sin(elevation);  // 0 at horizon, 1 at zenith
        if (d.intensity < 0.0f) d.intensity = 0.0f;
        return d;
    }
};

// ---------------------------------------------------------------------------
// ApplyLight — scales the RGB channels of an RGBA8888 pixel by [0, 1].
// Uses integer SWAR (SIMD-within-a-register) fixed-point arithmetic:
// packs R and B into one word and multiplies them in parallel,
// cutting multiplication and bit-shifting instructions by half.
// Alpha is preserved unchanged.
// ---------------------------------------------------------------------------
inline uint32_t ApplyLight(uint32_t color, float intensity) {
    if (intensity >= 1.0f) { return color; }
    if (intensity <= 0.0f) { return color & 0xFFu; }

    const uint32_t scale = static_cast<uint32_t>(intensity * 256.0f);
    const uint32_t rb = (color >> 8) & 0x00FF00FFu;
    const uint32_t g  = (color >> 16) & 0xFFu;

    const uint32_t scaledRb = ((rb * scale) >> 8) & 0x00FF00FFu;
    const uint32_t scaledG  = (g * scale) >> 8;

    return (scaledRb << 8) | (scaledG << 16) | (color & 0xFFu);
}

// ---------------------------------------------------------------------------
// ComputeLighting — accumulates point-light + optional directional sun light.
//
// sunLight.intensity == 0 → outdoor sun contribution disabled (indoor sectors).
// sqrt-free point lights: falloff = (1 - distSq/radiusSq)^2.
// ---------------------------------------------------------------------------
inline float ComputeLighting(float worldX, float worldY, float worldZ,
                              const std::vector<PointLight>& lights,
                              float ambientLevel,
                              const DirectionalLight& sunLight) {
    float total = ambientLevel;

    // Directional sun contribution — flat shading (no normals in 2.5D).
    // Outdoor surfaces get the full sun intensity scaled by elevation.
    // We scale by 0.6 so even at noon the sum ambient+sun stays ≤ 1.
    total += sunLight.intensity * 0.6f;

    for (const PointLight& light : lights) {
        const float dx = worldX - light.position.x;
        const float dy = worldY - light.position.y;
        const float dz = worldZ - light.position.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq >= light.radiusSq) { continue; }

        const float t    = distSq / light.radiusSq;
        const float atten = (1.0f - t) * (1.0f - t) * light.intensity;
        total += atten;
    }
    return total < 1.0f ? total : 1.0f;
}
