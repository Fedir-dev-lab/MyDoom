#include "SpriteRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "Framebuffer.h"
#include "Lighting.h"
#include "Texture.h"

namespace {

constexpr float kNearPlane = 0.05f;

// Camera-space vertex with UV and lighting.
struct BillVert {
    float vx, vy, vz;
    float u, v, lit;
};

// Projected vertex: screen X/Y + attributes divided by depth.
struct ProjVert {
    float sx, sy;
    float invZ, uZ, vZ, litZ;
};

// Sutherland-Hodgman near-plane clip for a quad (4 verts, CCW).
std::vector<BillVert> ClipNear(const std::array<BillVert, 4>& in) {
    std::vector<BillVert> out;
    out.reserve(6);
    for (int i = 0; i < 4; ++i) {
        const BillVert& a = in[i];
        const BillVert& b = in[(i + 1) % 4];
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

ProjVert Project(const BillVert& w, const Camera& cam) {
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
// Transparent texels (alpha == 0) are skipped.
void FillTextured(Framebuffer& fb, const std::vector<ProjVert>& poly,
                  const Texture& tex, int sw, int sh, bool highlight) {
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
            const int j = (i + 1 == n) ? 0 : i + 1;
            const ProjVert& a = poly[i];
            const ProjVert& b = poly[j];
            float ax,ay,ai,au,av,al, bx,by,bi,bu,bv,bl;
            if (a.sy <= b.sy) {
                ax=a.sx; ay=a.sy; ai=a.invZ; au=a.uZ; av=a.vZ; al=a.litZ;
                bx=b.sx; by=b.sy; bi=b.invZ; bu=b.uZ; bv=b.vZ; bl=b.litZ;
            } else {
                ax=b.sx; ay=b.sy; ai=b.invZ; au=b.uZ; av=b.vZ; al=b.litZ;
                bx=a.sx; by=a.sy; bi=a.invZ; bu=a.uZ; bv=a.vZ; bl=a.litZ;
            }
            const float dy = by - ay;
            if (dy < 1e-6f) { continue; }
            if (yc >= ay && yc < by) {
                const float t = (yc - ay) / dy;
                if (nc < 8) {
                    cr[nc++] = {
                        ax+t*(bx-ax), ai+t*(bi-ai),
                        au+t*(bu-au), av+t*(bv-av), al+t*(bl-al)
                    };
                }
            }
        }
        if (nc < 2) { continue; }

        // Sort crossings left → right.
        for (int i = 1; i < nc; ++i) {
            const Cross key = cr[i];
            int j = i - 1;
            while (j >= 0 && cr[j].x > key.x) { cr[j+1] = cr[j]; --j; }
            cr[j+1] = key;
        }

        for (int p = 0; p + 1 < nc; p += 2) {
            const float xl = cr[p].x, xr = cr[p+1].x;
            const int xli = std::max(0,    static_cast<int>(std::ceil(xl)));
            const int xri = std::min(sw-1, static_cast<int>(std::floor(xr)));
            if (xli > xri) { continue; }

            const float span = xr - xl;
            const float isp  = (span > 1e-6f) ? 1.0f / span : 0.0f;
            const float dI = (cr[p+1].invZ - cr[p].invZ) * isp;
            const float dU = (cr[p+1].uZ   - cr[p].uZ)   * isp;
            const float dV = (cr[p+1].vZ   - cr[p].vZ)   * isp;
            const float dL = (cr[p+1].litZ  - cr[p].litZ) * isp;
            const float offs = static_cast<float>(xli) + 0.5f - xl;
            float invZ = cr[p].invZ + dI*offs;
            float uZ   = cr[p].uZ   + dU*offs;
            float vZ   = cr[p].vZ   + dV*offs;
            float litZ = cr[p].litZ + dL*offs;
            const size_t row = static_cast<size_t>(y) * sw;

            for (int x = xli; x <= xri; ++x,
                     invZ+=dI, uZ+=dU, vZ+=dV, litZ+=dL) {
                if (invZ <= 0.0f) { continue; }
                const float d = 1.0f / invZ;
                const float u = uZ * d;
                const float v = vZ * d;
                const uint32_t color = tex.Sample(u, v);
                if ((color & 0xFFu) == 0) { continue; } // transparent
                const uint32_t final = highlight
                    ? Framebuffer::MakeColor(255, 220, 220)
                    : ApplyLight(color, litZ * d);
                fb.SetPixelDepthFast(row + x, final, d);
            }
        }
    }
}

} // namespace

SpriteRenderer::SpriteRenderer(int screenWidth, int screenHeight,
                               float fieldOfViewRadians)
    : m_screenWidth(screenWidth),
      m_screenHeight(screenHeight) {
    (void)fieldOfViewRadians;
}

void SpriteRenderer::DrawSprite(Framebuffer& framebuffer, const Camera& camera,
                                const Vector3& position, float width, float height,
                                const std::vector<PointLight>& lights, float ambientLight,
                                const DirectionalLight& sunLight,
                                const Texture& texture, bool highlight) const {
    // -----------------------------------------------------------------------
    // Billboard quad: 4 world-space corners always facing the camera.
    //
    // The billboard is a vertical rectangle in world space:
    //   - centred at (position.x, position.y + height/2, position.z)
    //   - width  along camera.right (the horizontal camera axis)
    //   - height along world Y (always upright, never tilts with pitch)
    //
    // The 4 corners are transformed through camera.ToView() — the full 3D
    // camera — then clipped, projected and rasterised exactly like a wall quad.
    // This guarantees the sprite sits correctly in 3D space at all pitch angles.
    // -----------------------------------------------------------------------

    // Camera right vector is horizontal (pitch-independent): (cosYaw, 0, -sinYaw).
    // Scale it to half the sprite width to get the left/right offsets.
    const float hr = width * 0.5f;
    const Vector3 rightOffset {
        camera.right.x * hr,
        0.0f,
        camera.right.z * hr
    };

    // 4 world-space corners: bottom-left, bottom-right, top-right, top-left.
    const Vector3 bl { position.x - rightOffset.x, position.y,          position.z - rightOffset.z };
    const Vector3 br { position.x + rightOffset.x, position.y,          position.z + rightOffset.z };
    const Vector3 tr { position.x + rightOffset.x, position.y + height, position.z + rightOffset.z };
    const Vector3 tl { position.x - rightOffset.x, position.y + height, position.z - rightOffset.z };

    // Per-corner lighting (Gouraud — cheap, matches WallRenderer).
    const float litBL = ComputeLighting(bl.x, bl.y, bl.z, lights, ambientLight, sunLight);
    const float litBR = ComputeLighting(br.x, br.y, br.z, lights, ambientLight, sunLight);
    const float litTR = ComputeLighting(tr.x, tr.y, tr.z, lights, ambientLight, sunLight);
    const float litTL = ComputeLighting(tl.x, tl.y, tl.z, lights, ambientLight, sunLight);

    // Transform to camera space through the full 3D basis.
    const auto toView = [&](const Vector3& p) -> std::array<float, 3> {
        const Camera::View v = camera.ToView(p);
        return {v.x, v.y, v.z};
    };

    const auto vbl = toView(bl);
    const auto vbr = toView(br);
    const auto vtr = toView(tr);
    const auto vtl = toView(tl);

    // Build the quad: CCW winding (bl → br → tr → tl).
    // UV: u=0 left, u=1 right; v=0 top, v=1 bottom.
    const std::array<BillVert, 4> verts = {{
        { vbl[0], vbl[1], vbl[2],  0.f, 1.f, litBL },
        { vbr[0], vbr[1], vbr[2],  1.f, 1.f, litBR },
        { vtr[0], vtr[1], vtr[2],  1.f, 0.f, litTR },
        { vtl[0], vtl[1], vtl[2],  0.f, 0.f, litTL },
    }};

    std::vector<BillVert> clipped = ClipNear(verts);
    if (clipped.size() < 3) { return; }

    std::vector<ProjVert> poly;
    poly.reserve(clipped.size());
    for (const BillVert& w : clipped) { poly.push_back(Project(w, camera)); }

    FillTextured(framebuffer, poly, texture,
                 m_screenWidth, m_screenHeight, highlight);
}
