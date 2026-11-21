#pragma once

// ---------------------------------------------------------------------------
// Frustum — 6-plane view frustum derived from Camera.
//
// Coordinate system: X/Z horizontal, Y vertical. yaw 0 looks toward +Z.
// All math is pure C++20, no external libraries.
//
// Usage (once per frame, before rendering objects):
//   Frustum frustum;
//   frustum.Build(cam);
//   if (frustum.TestSphere(pos, radius)) { /* draw */ }
// ---------------------------------------------------------------------------

#include "Camera.h"
#include "core/Vector3.h"

// A half-space: points with Dot(normal, p) + d >= 0 are on the visible side.
struct FrustumPlane {
    float nx, ny, nz, d;

    // Signed distance from point to plane (positive = visible side).
    float Distance(float px, float py, float pz) const {
        return nx * px + ny * py + nz * pz + d;
    }
};

struct Frustum {
    // Plane indices
    enum { Near = 0, Far = 1, Left = 2, Right = 3, Top = 4, Bottom = 5 };
    FrustumPlane planes[6];

    // Build frustum planes from the camera.
    // farDist = distance to the far clipping plane (default 400.0f).
    void Build(const Camera& cam, float farDist = 400.0f) {
        // Camera basis (already orthonormal)
        const float rx = cam.right.x,   ry = cam.right.y,   rz = cam.right.z;
        const float ux = cam.up.x,      uy = cam.up.y,      uz = cam.up.z;
        const float fx = cam.forward.x, fy = cam.forward.y, fz = cam.forward.z;

        const float px = cam.position.x;
        const float py = cam.position.y;
        const float pz = cam.position.z;

        // focalLength / halfWidth gives cos/sin of half-horizontal-FOV.
        // We normalise the plane normals analytically.
        const float invF = cam.focalLength;        // focal (pixels→world ratio)
        const float hw   = cam.halfWidth;
        const float hh   = cam.halfHeight;

        // ---- Near plane: forward face of the near clip ----------------
        // Normal points forward (inward).
        {
            constexpr float kNear = 0.05f;
            planes[Near].nx = fx;
            planes[Near].ny = fy;
            planes[Near].nz = fz;
            planes[Near].d  = -(fx * px + fy * py + fz * pz) - kNear;
        }

        // ---- Far plane: normal = -forward -----------------------------
        {
            planes[Far].nx = -fx;
            planes[Far].ny = -fy;
            planes[Far].nz = -fz;
            planes[Far].d  = (fx * px + fy * py + fz * pz) + farDist;
        }

        // ---- Left / Right planes --------------------------------------
        // screenX = hw + (vx/vz)*focal >= 0  =>  focal*vx + hw*vz >= 0
        // Normal = (focal * right + hw * forward) / lenH
        {
            const float lenH = std::sqrt(invF * invF + hw * hw);
            // Left: n = (invF * right + hw * forward) / lenH
            {
                const float nnx = (invF * rx + hw * fx) / lenH;
                const float nny = (invF * ry + hw * fy) / lenH;
                const float nnz = (invF * rz + hw * fz) / lenH;
                planes[Left].nx = nnx; planes[Left].ny = nny; planes[Left].nz = nnz;
                planes[Left].d  = -(nnx * px + nny * py + nnz * pz);
            }
            // Right: n = (-invF * right + hw * forward) / lenH
            {
                const float nnx = (-invF * rx + hw * fx) / lenH;
                const float nny = (-invF * ry + hw * fy) / lenH;
                const float nnz = (-invF * rz + hw * fz) / lenH;
                planes[Right].nx = nnx; planes[Right].ny = nny; planes[Right].nz = nnz;
                planes[Right].d  = -(nnx * px + nny * py + nnz * pz);
            }
        }

        // ---- Top / Bottom planes --------------------------------------
        // screenY = hh - (vy/vz)*focal:
        // sy >= 0       =>  hh*vz - focal*vy >= 0  (Top)
        // sy <= 2*hh    =>  focal*vy + hh*vz >= 0  (Bottom)
        {
            const float lenV = std::sqrt(invF * invF + hh * hh);
            // Bottom: n = (invF * up + hh * forward) / lenV
            {
                const float nnx = (invF * ux + hh * fx) / lenV;
                const float nny = (invF * uy + hh * fy) / lenV;
                const float nnz = (invF * uz + hh * fz) / lenV;
                planes[Bottom].nx = nnx; planes[Bottom].ny = nny; planes[Bottom].nz = nnz;
                planes[Bottom].d  = -(nnx * px + nny * py + nnz * pz);
            }
            // Top: n = (-invF * up + hh * forward) / lenV
            {
                const float nnx = (-invF * ux + hh * fx) / lenV;
                const float nny = (-invF * uy + hh * fy) / lenV;
                const float nnz = (-invF * uz + hh * fz) / lenV;
                planes[Top].nx = nnx; planes[Top].ny = nny; planes[Top].nz = nnz;
                planes[Top].d  = -(nnx * px + nny * py + nnz * pz);
            }
        }
    }

    // Returns true if the bounding sphere (center, radius) is at least partially
    // inside the frustum. False means the object can be safely skipped.
    bool TestSphere(const Vector3& center, float radius) const {
        const float cx = center.x, cy = center.y, cz = center.z;
        for (int i = 0; i < 6; ++i) {
            if (planes[i].Distance(cx, cy, cz) < -radius) {
                return false; // fully outside this plane
            }
        }
        return true;
    }

    // AABB test: conservative p-vertex test — returns false if the entire box
    // lies outside any one of the 6 planes; returns true if the box may
    // intersect or be inside the visible frustum.
    bool TestAABB(const Vector3& mn, const Vector3& mx) const {
        for (int i = 0; i < 6; ++i) {
            const FrustumPlane& pl = planes[i];
            // Pick vertex most in direction of plane normal (positive vertex).
            const float vx = pl.nx >= 0.0f ? mx.x : mn.x;
            const float vy = pl.ny >= 0.0f ? mx.y : mn.y;
            const float vz = pl.nz >= 0.0f ? mx.z : mn.z;
            if (pl.Distance(vx, vy, vz) < 0.0f) {
                return false; // positive vertex behind plane → fully outside
            }
        }
        return true;
    }
};
