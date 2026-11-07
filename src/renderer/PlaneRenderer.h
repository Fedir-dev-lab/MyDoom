#pragma once

#include <vector>

#include "core/Vector2.h"
#include "core/Vector3.h"
#include "renderer/Lighting.h"

class Framebuffer;
class Texture;

// Draws horizontal sector floors and ceilings with a perspective-correct
// mapping from screen pixels to points on the X/Z world plane.
// Per-row optimization: viewZ computed once per row (one division),
// worldX/worldZ stepped incrementally across each row (no per-pixel division).
class PlaneRenderer {
public:
    PlaneRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians);

    void DrawPlane(Framebuffer& framebuffer, const Vector3& cameraPosition,
                   float cosAngle, float sinAngle,
                   float height,
                   const std::vector<Vector2>& polygon,
                   float horizonY,
                   const std::vector<PointLight>& lights, float ambientLight,
                   const DirectionalLight& sunLight,
                   const Texture& texture) const;

private:
    int m_screenWidth;
    int m_screenHeight;
    float m_focalLength;
    float m_halfWidth;
};
