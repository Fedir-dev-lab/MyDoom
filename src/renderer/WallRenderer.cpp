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

// Camera-space vertex with texture and lighting attributes.
struct WallVert {
    float vx, vy, vz;   // camera space
    float u, v, lit;
};

// Projected vertex: screen X/Y + attributes divided by depth for
// perspective-correct linear interpolation across the polygon.
struct ProjVert {
    float sx, sy;
    float invZ, uOverZ, vOverZ, litOverZ;
};

// Sutherland–Hodgman clip against the near plane (vz >= near).
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
            out.push_back({
                a.vx + t*(b.vx-a.vx), a.vy + t*(b.vy-a.vy), kNearPlane,
                a.u  + t*(b.u -a.u),  a.v  + t*(b.v -a.v),
                a.lit + t*(b.lit-a.lit)
            });
        }
    }
    return out;
}

ProjVert Project(const WallVert& w, const Camera& cam) {
    const float invZ = 1.0f / w.vz;
    return {
        cam.halfWidth  + w.vx * invZ * cam.focalLength,
        cam.halfHeight - w.vy * invZ * cam.focalLength,
        invZ,
        w.u   * invZ,
        w.v   * invZ,
        w.lit * invZ
    };
}

// Scanline fill of a convex polygon with perspective-correct UV + lighting.
void FillTextured(Framebuffer& fb, const std::vector<ProjVert>& poly,
                  const Texture& tex, int sw, int sh) {
    const int n = static_cast<int>(poly.size());
    if (n < 3) { return; }

    float minY = poly[0].sy, maxY = poly[0].sy;
    for (int i = 1; i < n; ++i) {
        if (poly[i].sy < minY) minY = poly[i].sy;
        if (poly[i].sy > maxY) maxY = poly[i].sy;
    }
    const int y0 = std::max(0,    static_cast<int>(std::ceil(minY)));
    const int y1 = std::min(sh-1, static_cast<int>(std::floor(maxY)));

    struct Cross { float x, invZ, uZ, vZ, litZ; };

    for (int y = y0; y <= y1; ++y) {
        const float yc = static_cast<float>(y) + 0.5f;
        std::array<Cross, 8> cr;
        int nc = 0;

        for (int i = 0; i < n; ++i) {
            const int j = (i+1==n) ? 0 : i+1;
            const ProjVert& a = poly[i];
            const ProjVert& b = poly[j];
            float ax,ay,ai,au,av,al, bx,by,bi,bu,bv,bl;
            if (a.sy <= b.sy) {
                ax=a.sx; ay=a.sy; ai=a.invZ; au=a.uOverZ; av=a.vOverZ; al=a.litOverZ;
                bx=b.sx; by=b.sy; bi=b.invZ; bu=b.uOverZ; bv=b.vOverZ; bl=b.litOverZ;
            } else {
                ax=b.sx; ay=b.sy; ai=b.invZ; au=b.uOverZ; av=b.vOverZ; al=b.litOverZ;
                bx=a.sx; by=a.sy; bi=a.invZ; bu=a.uOverZ; bv=a.vOverZ; bl=a.litOverZ;
            }
            const float dy = by - ay;
            if (dy < 1e-6f) { continue; }
            if (yc >= ay && yc < by) {
                const float t = (yc - ay) / dy;
                if (nc < 8) {
                    cr[nc++] = {
                        ax + t*(bx-ax), ai + t*(bi-ai),
                        au + t*(bu-au), av + t*(bv-av), al + t*(bl-al)
                    };
                }
            }
        }
        if (nc < 2) { continue; }

        // Sort crossings left→right.
        for (int i = 1; i < nc; ++i) {
            const Cross key = cr[i];
            int j = i-1;
            while (j >= 0 && cr[j].x > key.x) { cr[j+1]=cr[j]; --j; }
            cr[j+1] = key;
        }

        for (int p = 0; p+1 < nc; p += 2) {
            const float xl = cr[p].x, xr = cr[p+1].x;
            const int xli = std::max(0,    static_cast<int>(std::ceil(xl)));
            const int xri = std::min(sw-1, static_cast<int>(std::floor(xr)));
            if (xli > xri) { continue; }
            const float span = xr - xl;
            const float isp  = (span > 1e-6f) ? 1.0f/span : 0.0f;
            const float dI   = (cr[p+1].invZ - cr[p].invZ) * isp;
            const float dU   = (cr[p+1].uZ   - cr[p].uZ)   * isp;
            const float dV   = (cr[p+1].vZ   - cr[p].vZ)   * isp;
            const float dL   = (cr[p+1].litZ  - cr[p].litZ) * isp;
            const float offs = static_cast<float>(xli) + 0.5f - xl;
            float invZ = cr[p].invZ + dI*offs;
            float uZ   = cr[p].uZ   + dU*offs;
            float vZ   = cr[p].vZ   + dV*offs;
            float litZ = cr[p].litZ + dL*offs;
            const size_t row = static_cast<size_t>(y) * sw;
            for (int x = xli; x <= xri; ++x, invZ+=dI, uZ+=dU, vZ+=dV, litZ+=dL) {
                if (invZ > 0.0f) {
                    const float d = 1.0f / invZ;
                    fb.SetPixelDepthFast(row+x,
                        ApplyLight(tex.Sample(uZ*d, vZ*d), litZ*d), d);
                }
            }
        }
    }
}

} // namespace

WallRenderer::WallRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians)
    : m_screenWidth(screenWidth),
      m_screenHeight(screenHeight) {
    (void)fieldOfViewRadians;
}

void WallRenderer::DrawWall(Framebuffer& framebuffer, const Camera& camera,
                            Vector3 start, Vector3 end,
                            float bottomHeight, float topHeight,
                            const std::vector<PointLight>& lights, float ambientLight,
                            const DirectionalLight& sunLight,
                            const Texture& texture) const {
    // Build a quad of 4 world-space corners with per-corner Gouraud lighting,
    // transform all corners through the full 3D camera, clip, project, fill.
    std::array<WallVert, 4> verts = {{
        {start.x, bottomHeight, start.z, 0.f, 1.f,
            ComputeLighting(start.x, bottomHeight, start.z, lights, ambientLight, sunLight)},
        {end.x,   bottomHeight, end.z,   1.f, 1.f,
            ComputeLighting(end.x,   bottomHeight, end.z,   lights, ambientLight, sunLight)},
        {end.x,   topHeight,    end.z,   1.f, 0.f,
            ComputeLighting(end.x,   topHeight,    end.z,   lights, ambientLight, sunLight)},
        {start.x, topHeight,    start.z, 0.f, 0.f,
            ComputeLighting(start.x, topHeight,    start.z, lights, ambientLight, sunLight)},
    }};

    // Transform to camera space using the full 3D basis (yaw + pitch).
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
