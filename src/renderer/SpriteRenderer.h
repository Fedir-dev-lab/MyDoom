#pragma once

#include <vector>

#include "core/Vector3.h"
#include "renderer/Lighting.h"

class Framebuffer;
class Texture;

// Draws vertical camera-facing billboards such as decorations and enemies.
class SpriteRenderer {
public:
    SpriteRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians);

    // cosAngle/sinAngle are precomputed by the caller once per frame.
    void DrawSprite(Framebuffer& framebuffer, const Vector3& cameraPosition,
                    float cosAngle, float sinAngle,
                    const Vector3& position,
                    float width, float height,
                    float horizonY,
                    const std::vector<PointLight>& lights, float ambientLight,
                    const DirectionalLight& sunLight,
                    const Texture& texture,
                    bool highlight = false) const;

private:
    int m_screenWidth;
    int m_screenHeight;
    float m_focalLength;
    float m_halfWidth;
};
