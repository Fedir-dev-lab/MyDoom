#pragma once

#include <vector>

#include "core/Vector3.h"
#include "renderer/Camera.h"
#include "renderer/Lighting.h"

class Framebuffer;
class Texture;

// Renders vertical wall quads as true 3D polygons. Because the camera now
// pitches for real, a wall is a general on-screen quadrilateral (its vertical
// edges slant under pitch), so it is rasterised as a perspective-correct
// textured quad (near-clipped, Gouraud-lit, z-buffered) rather than column by
// column.
class WallRenderer {
public:
    WallRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians);

    void DrawWall(Framebuffer& framebuffer, const Camera& camera,
                  Vector3 start, Vector3 end,
                  float bottomHeight, float topHeight,
                  const std::vector<PointLight>& lights, float ambientLight,
                  const DirectionalLight& sunLight,
                  const Texture& texture,
                  float u0 = 0.0f, float u1 = -1.0f,
                  float v0 = 0.0f, float v1 = -1.0f) const;

private:
    int m_screenWidth;
    int m_screenHeight;
};
