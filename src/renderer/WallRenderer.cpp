#include "WallRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "Framebuffer.h"
#include "Lighting.h"
#include "Texture.h"

namespace {

constexpr float kNearPlane = 0.05f;

// A wall quad vertex in camera space carrying perspective-correct attributes.
// u runs along the wall (0 at start → 1 at end), v bottom(1)→top(0).
struct WallVert {
    float vx, vy, vz;
    float u, v, lit;
};

// Projected vertex: screen position + attributes pre-divided by viewZ so they
// interpolate linearly in screen space (perspective-correct).
struct ProjVert {
    float sx, sy;
    float invZ, uOverZ, vOverZ, litOverZ;
};

// Sutherland–Hodgman clip of the quad against the near plane (vz >= near).
std::vector<WallVert> ClipNear(const std::array<WallVert, 4>& in) {
    std::vector<WallVert> out;
    out.reserve(6);
    for (int i = 0; i < 4; ++i) {
        const WallVert& a = in[i];
        const WallVert& b = in[(i + 1) % 4];
        const bool aIn = a.vz >= kNearPlane;
        const bool bIn = b.vz >= kNearPlane;
        if (aIn) { out.push_back(a); }
        if (aIn != bIn) {
            const float t = (kNearPlane - a.vz) / (b.vz - a.vz);
            out.push_back({a.vx + t * (b.vx - a.vx), a.vy + t * (b.vy - a.vy), kNearPlane,
                           a.u + t * (b.u - a.u), a.v + t * (b.v - a.v),
                           a.lit + t * (b.lit - a.lit)});
        }
    }
    return out;
}

ProjVert Project(const WallVert& w, const Camera& cam) {
    const float invZ = 1.0f / w.vz;
    return {cam.halfWidth + w.vx * invZ * cam.focalLength,
            cam.halfHeight - w.vy * invZ * cam.focalLength,
            invZ, w.u * invZ, w.v * invZ, w.lit * invZ};
}

// Scanline fill of a convex projected polygon with perspective-correct UV and
// lighting and a per-pixel z-test (depth = 1/invZ = viewZ).
void FillTextured(Framebuffer& framebuffer, const std::vector<ProjVert>& poly,
                  const Texture& texture, int screenW, int screenH) {
    const int n = static_cast<int>(poly.size());
    if (n < 3) { return; }

    float minY = poly[0].sy, maxY = poly[0].sy;
    for (int i = 1; i < n; ++i) {
        if (poly[i].sy < minY) { minY = poly[i].sy; }
        if (poly[i].sy > maxY) { maxY = poly[i].sy; }
    }
    const int y0 = std::max(0, static_cast<int>(std::ceil(minY)));
    const int y1 = std::min(screenH - 1, static_cast<int>(std::floor(maxY)));

    struct Cross { float x, invZ, uOverZ, vOverZ, litOverZ; };

    for (int y = y0; y <= y1; ++y) {
        const float yc = static_cast<float>(y) + 0.5f;
        std::array<Cross, 8> cr;
        int nc = 0;

        for (int i = 0; i < n; ++i) {
            const int j = (i + 1 == n) ? 0 : i + 1;
            const ProjVert& a = poly[i];
            const ProjVert& b = poly[j];
            // Orient low→high (half-open [ay, by) avoids double-counting).
            float ax, ay, ainv, au, av, al, bx, by, binv, bu, bv, bl;
            if (a.sy <= b.sy) {
                ax=a.sx; ay=a.sy; ainv=a.invZ; au=a.uOverZ; av=a.vOverZ; al=a.litOverZ;
                bx=b.sx; by=b.sy; binv=b.invZ; bu=b.uOverZ; bv=b.vOverZ; bl=b.litOverZ;
            } else {
                ax=b.sx; ay=b.sy; ainv=b.invZ; au=b.uOverZ; av=b.vOverZ; al=b.litOverZ;
                bx=a.sx; by=a.sy; binv=a.invZ; bu=a.uOverZ; bv=a.vOverZ; bl=a.litOverZ;
            }
            const float dy = by - ay;
            if (dy < 1e-6f) { continue; }
            if (yc >= ay && yc < by) {
                const float t = (yc - ay) / dy;
                if (nc < 8) {
                    cr[nc].x      = ax + t * (bx - ax);
                    cr[nc].invZ   = ainv + t * (binv - ainv);
                    cr[nc].uOverZ = au  + t * (bu  - au);
                    cr[nc].vOverZ = av  + t * (bv  - av);
                    cr[nc].litOverZ = al + t * (bl - al);
                    ++nc;
                }
            }
        }
        if (nc < 2) { continue; }

        for (int i = 1; i < nc; ++i) {
            const Cross key = cr[i];
            int j = i - 1;
            while (j >= 0 && cr[j].x > key.x) { cr[j + 1] = cr[j]; --j; }
            cr[j + 1] = key;
        }

        for (int p = 0; p + 1 < nc; p += 2) {
            const float xl = cr[p].x, xr = cr[p + 1].x;
            const int xLi = std::max(0, static_cast<int>(std::ceil(xl)));
            const int xRi = std::min(screenW - 1, static_cast<int>(std::floor(xr)));
            if (xLi > xRi) { continue; }
            const float span = xr - xl;
            const float invSpan = (span > 1e-6f) ? 1.0f / span : 0.0f;
            const float dInvZ = (cr[p + 1].invZ     - cr[p].invZ)     * invSpan;
            const float dU   = (cr[p + 1].uOverZ   - cr[p].uOverZ)   * invSpan;
            const float dV   = (cr[p + 1].vOverZ   - cr[p].vOverZ)   * invSpan;
            const float dL   = (cr[p + 1].litOverZ - cr[p].litOverZ) * invSpan;
            const float offs = static_cast<float>(xLi) + 0.5f - xl;
            float invZ   = cr[p].invZ     + dInvZ * offs;
            float uOverZ = cr[p].uOverZ   + dU   * offs;
            float vOverZ = cr[p].vOverZ   + dV   * offs;
            float litOZ  = cr[p].litOverZ + dL   * offs;
            const size_t rowBase = static_cast<size_t>(y) * screenW;
            for (int x = xLi; x <= xRi; ++x,
                     invZ += dInvZ, uOverZ += dU, vOverZ += dV, litOZ += dL) {
                if (invZ > 0.0f) {
                    const float depth = 1.0f / invZ;
                    const float u = uOverZ * depth;
                    const float v = vOverZ * depth;
                    const float lit = litOZ * depth;
                    framebuffer.SetPixelDepthFast(rowBase + x,
                        ApplyLight(texture.Sample(u, v), lit), depth);
                }
            }
        }
    }
}

}  // namespace

WallRenderer::WallRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians)
    : m_screenWidth(screenWidth),
      m_screenHeight(screenHeight) {
    (void)fieldOfViewRadians;  // projection now comes from the Camera
}

void WallRenderer::DrawWall(Framebuffer& framebuffer, const Camera& camera,
                            Vector3 start, Vector3 end,
                            float bottomHeight, float topHeight,
                            const std::vector<PointLight>& lights, float ambientLight,
                            const DirectionalLight& sunLight,
                            const Texture& texture) const {
    // Quad corners (start→end along u, bottom→top along v), with per-corner
    // lighting so the fill is Gouraud-shaded (captures point-light falloff
    // across the wall without per-pixel light math).
    std::array<WallVert, 4> verts = {{
        {start.x, bottomHeight, start.z, 0.0f, 1.0f,
            ComputeLighting(start.x, bottomHeight, start.z, lights, ambientLight, sunLight)},
        {end.x,   bottomHeight, end.z,   1.0f, 1.0f,
            ComputeLighting(end.x,   bottomHeight, end.z,   lights, ambientLight, sunLight)},
        {end.x,   topHeight,    end.z,   1.0f, 0.0f,
            ComputeLighting(end.x,   topHeight,    end.z,   lights, ambientLight, sunLight)},
        {start.x, topHeight,    start.z, 0.0f, 0.0f,
            ComputeLighting(start.x, topHeight,    start.z, lights, ambientLight, sunLight)},
    }};

    for (WallVert& w : verts) {
        const Camera::View v = camera.ToView({w.vx, w.vy, w.vz});
        w.vx = v.x; w.vy = v.y; w.vz = v.z;
    }

    std::vector<WallVert> clipped = ClipNear(verts);
    if (clipped.size() < 3) { return; }

    std::vector<ProjVert> poly;
    poly.reserve(clipped.size());
    for (const WallVert& w : clipped) { poly.push_back(Project(w, camera)); }

    FillTextured(framebuffer, poly, texture, m_screenWidth, m_screenHeight);
}
