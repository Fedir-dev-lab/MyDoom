#pragma once

#include <cmath>

#include "core/Vector3.h"

// ---------------------------------------------------------------------------
// Camera — a true 3D camera basis (yaw + pitch) shared by every renderer.
//
// Previously each renderer transformed world points with yaw only and faked
// looking up/down by shifting the horizon line (horizonY = halfH - tan(pitch)
// * focal). That is a shear, not a rotation, so floors/ceilings squished and
// 3D solids sheared when pitching. This struct gives every renderer the same
// orthonormal basis and projection so pitch is a real rotation.
//
//   right    = ( cosθ,  0, -sinθ)            // camera X (unchanged by pitch)
//   forward  = ( sinθ cosφ, -sinφ, cosθ cosφ) // look dir, φ>0 looks down
//   up       = ( sinθ sinφ,  cosφ, cosθ sinφ)
//
// At pitch 0 this collapses to the old yaw-only transform:
//   viewX = rel·right = relX cosθ - relZ sinθ
//   viewY = rel·up    = relY
//   viewZ = rel·forward = relX sinθ + relZ cosθ
// and screenY = halfH - viewY/viewZ * focal == the old horizonY (=halfH) form,
// so existing behaviour is preserved exactly when not pitching.
// ---------------------------------------------------------------------------
struct Camera {
    Vector3 position;
    Vector3 right;
    Vector3 up;
    Vector3 forward;

    float focalLength = 1.0f;
    float halfWidth   = 0.0f;
    float halfHeight  = 0.0f;

    float cosPitch = 1.0f;
    float sinPitch = 0.0f;

    // Screen row of the true horizon (where the view ray is horizontal).
    // The player clamps pitch well away from ±90°, but guard regardless.
    float HorizonRow() const {
        float t;
        if (cosPitch > 1e-4f) {
            t = sinPitch / cosPitch;
        } else {
            t = (sinPitch > 0.0f) ? 8.0f : -8.0f;
        }
        return halfHeight - t * focalLength;
    }

    // World point -> camera space. viewZ > 0 means in front of the camera.
    struct View { float x, y, z; };
    View ToView(const Vector3& world) const {
        const Vector3 rel = world - position;
        return { Vector3::Dot(rel, right),
                 Vector3::Dot(rel, up),
                 Vector3::Dot(rel, forward) };
    }

    // Build from yaw θ and pitch φ (positive = look down), screen size, focal.
    static Camera Make(const Vector3& pos, float yaw, float pitch,
                       float focal, int rw, int rh) {
        const float cy = std::cos(yaw),  sy = std::sin(yaw);
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        Camera c;
        c.position   = pos;
        c.right      = { cy,  0.0f, -sy };
        c.forward    = { sy * cp, -sp, cy * cp };
        c.up         = { sy * sp,  cp, cy * sp };
        c.focalLength = focal;
        c.halfWidth   = rw * 0.5f;
        c.halfHeight  = rh * 0.5f;
        c.cosPitch    = cp;
        c.sinPitch    = sp;
        return c;
    }
};
