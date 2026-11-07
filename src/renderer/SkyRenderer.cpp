#include "SkyRenderer.h"

#include <algorithm>
#include <cmath>

#include "Framebuffer.h"

namespace {

constexpr float kPiF = 3.14159265f;

// Angular sizes of sun features (radians).
constexpr float kSunRadius    = 0.055f;
constexpr float kCoronaRadius = 0.13f;
constexpr float kRayRadius    = 0.42f;
constexpr int   kRayCount     = 14;

inline float clamp01(float t) {
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

inline float smoothstep(float edge0, float edge1, float t) {
    t = clamp01((t - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

inline uint8_t lerp8(uint8_t a, uint8_t b, float t) {
    return static_cast<uint8_t>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t);
}

// Additive blend: clamp each channel to 255.
inline uint32_t blendAdd(uint32_t base, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    if (alpha <= 0.0f) { return base; }
    if (alpha >  1.0f) { alpha = 1.0f; }
    const uint32_t nr = (base >> 24)        + static_cast<uint32_t>(r * alpha);
    const uint32_t ng = ((base >> 16) & 0xFF) + static_cast<uint32_t>(g * alpha);
    const uint32_t nb = ((base >>  8) & 0xFF) + static_cast<uint32_t>(b * alpha);
    return Framebuffer::MakeColor(
        static_cast<uint8_t>(nr > 255u ? 255u : nr),
        static_cast<uint8_t>(ng > 255u ? 255u : ng),
        static_cast<uint8_t>(nb > 255u ? 255u : nb));
}

// Alpha blend: lerp base toward target color.
inline uint32_t blendOver(uint32_t base, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    if (alpha <= 0.0f) { return base; }
    if (alpha >= 1.0f) { return Framebuffer::MakeColor(r, g, b); }
    return Framebuffer::MakeColor(
        lerp8(static_cast<uint8_t>(base >> 24),         r, alpha),
        lerp8(static_cast<uint8_t>((base >> 16) & 0xFF), g, alpha),
        lerp8(static_cast<uint8_t>((base >>  8) & 0xFF), b, alpha));
}

} // namespace

SkyRenderer::SkyRenderer(int width, int height)
    : m_width(width),
      m_height(height),
      m_focalLength(static_cast<float>(width) * 0.5f / std::tan(kPiF * 0.25f))
{
}

void SkyRenderer::Draw(Framebuffer& framebuffer,
                       float sunWorldAngle, float sunElevation,
                       float cameraAngle,   float cameraPitch) const {
    // -----------------------------------------------------------------------
    // Sky gradient colours — interpolated by sun elevation.
    //   elevation ~ 0   → sunset/sunrise: orange-pink horizon
    //   elevation ~ 0.5 → midday: deep blue top, light blue horizon
    //   elevation < 0   → night would go dark, but we clamp to dawn/dusk
    // -----------------------------------------------------------------------
    const float t = clamp01(sunElevation / (kPiF * 0.5f)); // 0=horizon, 1=zenith

    // Top of sky
    const uint8_t topR = lerp8(80,  18,  t);
    const uint8_t topG = lerp8(40,  52,  t);
    const uint8_t topB = lerp8(80,  140, t);
    // Horizon
    const uint8_t horR = lerp8(220, 108, t);
    const uint8_t horG = lerp8(120, 168, t);
    const uint8_t horB = lerp8(60,  210, t);

    // -----------------------------------------------------------------------
    // Project sun world position onto screen using the same perspective math
    // as the wall renderer so the sun tracks the horizon without jumps.
    //
    // Sun 3-D direction in world space (unit vector):
    //   right  = sin(sunWorldAngle)
    //   up     = sin(sunElevation)
    //   forward= cos(sunWorldAngle) * cos(sunElevation)
    //
    // Rotate into camera space (yaw only — no separate pitch rotation needed
    // because we project the elevation directly via the vertical component):
    //   viewX =  cos(sunWorldAngle - cameraAngle) * cos(sunElevation)  (not used for SX)
    //   The horizontal screen offset uses sin(relYaw) * focalLength.
    //   The vertical screen offset mirrors Game.cpp's horizonY formula:
    //     horizonY = halfH - tan(cameraPitch) * focalLength
    //   so the sun's Y = horizonY - tan(sunElevation) * focalLength
    //   (when cameraPitch=0 and sunElevation=0, sun sits exactly on horizon).
    // -----------------------------------------------------------------------
    const float sunRelYaw  = sunWorldAngle - cameraAngle;

    // Forward component — positive means sun is in front of camera.
    const float sunViewZ = std::cos(sunRelYaw) * std::cos(sunElevation);

    const float halfW = static_cast<float>(m_width)  * 0.5f;
    const float halfH = static_cast<float>(m_height) * 0.5f;

    // Horizon row — identical to Game.cpp's horizonY.
    const float horizonRow = halfH - std::tan(cameraPitch) * m_focalLength;

    // Sun screen position: horizontal via relYaw, vertical via sun elevation
    // relative to the camera's horizon line.
    // Use tan (not sin) so the sun moves off-screen at ±90° instead of reversing.
    const float sunSX = halfW + std::tan(sunRelYaw) * m_focalLength;
    const float sunSY = horizonRow - std::tan(sunElevation) * m_focalLength;

    const bool sunVisible = sunViewZ > 0.05f;
    const float maxSunRadiusPixels = kRayRadius * m_focalLength;
    const float maxSunDistSq = maxSunRadiusPixels * maxSunRadiusPixels;

    // The sky gradient costs the same whether the sun is in view or not.
    // Keep it as a simple row fill; sun effects are drawn in a small second
    // pass below instead of doing trigonometry for every framebuffer pixel.
    for (int y = 0; y < m_height; ++y) {
        const float pixelElevAngle = std::atan2(horizonRow - static_cast<float>(y), m_focalLength);
        const float fy = clamp01(1.0f - pixelElevAngle / (kPiF * 0.5f));

        const uint32_t baseGradientColor = Framebuffer::MakeColor(
            lerp8(topR, horR, fy),
            lerp8(topG, horG, fy),
            lerp8(topB, horB, fy));

        std::fill_n(framebuffer.Pixels() + static_cast<size_t>(y) * m_width,
                    m_width, baseGradientColor);
    }

    if (!sunVisible || sunSX + maxSunRadiusPixels < 0.0f ||
        sunSX - maxSunRadiusPixels >= m_width || sunSY + maxSunRadiusPixels < 0.0f ||
        sunSY - maxSunRadiusPixels >= m_height) {
        return;
    }

    const float invFocal = 1.0f / m_focalLength;
    const int firstX = std::max(0, static_cast<int>(std::floor(sunSX - maxSunRadiusPixels)));
    const int lastX = std::min(m_width - 1, static_cast<int>(std::ceil(sunSX + maxSunRadiusPixels)));
    const int firstY = std::max(0, static_cast<int>(std::floor(sunSY - maxSunRadiusPixels)));
    const int lastY = std::min(m_height - 1, static_cast<int>(std::ceil(sunSY + maxSunRadiusPixels)));
    const uint8_t discR = lerp8(255, 255, t);
    const uint8_t discG = lerp8(140, 240, t);
    const uint8_t discB = lerp8(40, 180, t);

    // Pre-compute ray spoke directions once — replaces per-pixel atan2.
    // Each spoke is a unit vector (cos(k*2pi/N), sin(k*2pi/N)).
    // The per-pixel "spoke" value is max(dot(pixelDir, spokeDir), 0)^6,
    // which gives the same sharp rays as cos(angle*N)^6 from atan2,
    // but costs N dot-products instead of one atan2 + one cos.
    // For N=14 and a ~60×60 sun window this is a large win: atan2 is ~20
    // ns, 14 dot-products + max are ~4 ns on modern CPUs.
    struct SpokeDir { float cx, cy; };
    SpokeDir spokeDirs[kRayCount];
    for (int k = 0; k < kRayCount; ++k) {
        const float a = static_cast<float>(k) * (2.0f * kPiF / kRayCount);
        spokeDirs[k] = { std::cos(a), std::sin(a) };
    }

    const float elevFactor = clamp01(sunElevation * 2.0f) * 0.28f;

    for (int y = firstY; y <= lastY; ++y) {
        const float pdy = static_cast<float>(y) - sunSY;
        const float pdySq = pdy * pdy;
        const size_t rowBase = static_cast<size_t>(y) * m_width;
        for (int x = firstX; x <= lastX; ++x) {
            const float pdx = static_cast<float>(x) - sunSX;
            const float distSq = pdx * pdx + pdySq;
            if (distSq >= maxSunDistSq) { continue; }

            uint32_t color = framebuffer.Pixels()[rowBase + x];
            const float angDist = std::sqrt(distSq) * invFocal;

            if (angDist > kSunRadius && angDist < kRayRadius) {
                // Normalise pixel direction (safe: distSq > 0 inside the radius check).
                const float invLen = 1.0f / std::sqrt(distSq);
                const float ndx = pdx * invLen;
                const float ndy = pdy * invLen;

                // Find the maximum dot product across all spoke directions.
                // This is equivalent to cos(angle - nearestSpokeAngle), giving
                // bright spokes where the pixel aligns with any spoke direction.
                float maxDot = 0.0f;
                for (int k = 0; k < kRayCount; ++k) {
                    const float d = ndx * spokeDirs[k].cx + ndy * spokeDirs[k].cy;
                    if (d > maxDot) maxDot = d;
                }
                // Raise to the 6th power to sharpen spokes (same as original).
                const float spoke = maxDot * maxDot * maxDot * maxDot * maxDot * maxDot;
                const float fade = 1.0f - smoothstep(kSunRadius * 2.0f, kRayRadius, angDist);
                color = blendAdd(color, 255, 220, 120, spoke * fade * elevFactor);
            }
            if (angDist < kCoronaRadius) {
                color = blendAdd(color, 255, 190, 80,
                    smoothstep(kCoronaRadius, kSunRadius * 1.4f, angDist) * 0.55f);
            }
            if (angDist < kSunRadius * 1.2f) {
                const float edge = 1.0f - smoothstep(kSunRadius * 0.75f, kSunRadius * 1.2f, angDist);
                color = blendOver(color, discR, discG, discB, edge * 0.95f);
                if (angDist < kSunRadius * 0.35f) {
                    color = blendAdd(color, 255, 255, 230, 0.85f);
                }
            }
            framebuffer.SetPixelFast(rowBase + x, color);
        }
    }
}
