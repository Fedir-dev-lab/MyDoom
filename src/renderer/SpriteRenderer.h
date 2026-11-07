#pragma once

#include <vector>

#include "core/Vector3.h"
#include "renderer/Camera.h"
#include "renderer/Lighting.h"

class Framebuffer;
class Texture;

// Draws camera-facing billboards. The billboard stays screen-aligned (it does
// not tilt with pitch, like classic Doom sprites) but its centre is projected
// through the true 3D camera and its size scaled by the true-pitch depth, so
// it sits correctly against the pitched floor/ceiling.
class SpriteRenderer {
public:
    SpriteRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians);

    void DrawSprite(Framebuffer& framebuffer, const Camera& camera,
                    const Vector3& position, float width, float height,
                    const std::vector<PointLight>& lights, float ambientLight,
                    const DirectionalLight& sunLight,
                    const Texture& texture, bool highlight = false) const;

private:
    int m_screenWidth;
    int m_screenHeight;
};
