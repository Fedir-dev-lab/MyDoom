#pragma once

#include <cstdint>

class Framebuffer;

// Renders a dynamic sky: gradient that reacts to sun elevation, sun disc
// with corona and god rays. The sun position is passed each frame as
// independent world-space angles so it moves regardless of camera direction.
class SkyRenderer {
public:
    SkyRenderer(int width, int height);

    // sunWorldAngle — horizontal world direction of the sun (radians, same
    //                 coordinate system as Player::Angle).
    // sunElevation  — angle above horizon (radians). 0=sunset, pi/2=zenith.
    // cameraAngle   — player yaw (to project sun onto screen).
    // cameraPitch   — player pitch (to project sun onto screen).
    void Draw(Framebuffer& framebuffer,
              float sunWorldAngle, float sunElevation,
              float cameraAngle,   float cameraPitch) const;

private:
    int   m_width;
    int   m_height;
    float m_focalLength;
};
