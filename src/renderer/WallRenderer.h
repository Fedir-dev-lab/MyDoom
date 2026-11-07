#pragma once

#include <cstdint>
#include <vector>

#include "core/Vector3.h"
#include "renderer/Lighting.h"

class Framebuffer;
class Texture;

// Projects vertical walls from the X/Z world plane into the framebuffer.
class WallRenderer {
public:
    WallRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians);

    // cosAngle/sinAngle are std::cos/sin of the camera yaw, precomputed by
    // the caller once per frame so 17 wall draws don't recompute them.
    void DrawWall(Framebuffer& framebuffer,
                  const Vector3& cameraPosition, float cosAngle, float sinAngle,
                  Vector3 start, Vector3 end,
                  float bottomHeight, float topHeight,
                  float horizonY,
                  const std::vector<PointLight>& lights, float ambientLight,
                  const DirectionalLight& sunLight,
                  const Texture& texture) const;

private:
    struct ViewPoint {
        float x;
        float z;
    };

    ViewPoint ToViewSpace(const Vector3& point,
                          const Vector3& cameraPosition,
                          float cosAngle, float sinAngle) const;

    int m_screenWidth;
    int m_screenHeight;
    float m_focalLength;
    float m_halfWidth;
};
