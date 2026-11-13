#include "WallRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "Framebuffer.h"
#include "Lighting.h"
#include "Texture.h"

namespace {

constexpr float kNearPlane     = 0.05f;
constexpr float kWallThickness = 0.18f;  // world-unit depth of solid wall

// ---------------------------------------------------------------------------
// Camera-space vertex (plain position only)
// ---------------------------------------------------------------------------
struct CamVert { float x, y, z; };

// Screen vertex: screen-space XY + depth + optional perspective-correct UVs/lit.
struct ScrVert {
    float sx, sy, invZ;
    float uOverZ = 0.f, vOverZ = 0.f, litOverZ = 1.f;
};

// ---------------------------------------------------------------------------
// Near-plane Sutherland–Hodgman clip, generic over vertex type.
// Lerp is a callable (const V&, const V&, float t) -> V.
// ---------------------------------------------------------------------------
template<typename V, typename Lerp>
std::vector<V> ClipNear(const std::vector<V>& in, Lerp lerp) {
    std::vector<V> out;
    out.reserve(in.size() + 1);
    const int n = static_cast<int>(in.size());
    for (int i = 0; i < n; ++i) {
        const V& a = in[i];
        const V& b = in[(i + 1) % n];
        const bool aIn = a.z >= kNearPlane;
        const bool bIn = b.z >= kNearPlane;
        if (aIn) out.push_back(a);
        if (aIn != bIn) {
            const float t = (kNearPlane - a.z) / (b.z - a.z);
            out.push_back(lerp(a, b, t));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Project one camera-space vertex to screen.
// ---------------------------------------------------------------------------
ScrVert Project(const CamVert& v, const Camera& cam) {
    const float invZ = 1.f / v.z;
    return { cam.halfWidth  + v.x * invZ * cam.focalLength,
             cam.halfHeight - v.y * invZ * cam.focalLength,
             invZ };
}

// ---------------------------------------------------------------------------
// Flat-shaded scanline fill (perspective-correct depth only).
// ---------------------------------------------------------------------------
void FillFlat(Framebuffer& fb, const std::vector<ScrVert>& poly,
              uint32_t color, int sw, int sh) {
    const int n = static_cast<int>(poly.size());
    if (n < 3) return;

    float minY = poly[0].sy, maxY = poly[0].sy;
    for (int i = 1; i < n; ++i) {
        if (poly[i].sy < minY) minY = poly[i].sy;
        if (poly[i].sy > maxY) maxY = poly[i].sy;
    }
    const int y0 = std::max(0,    static_cast<int>(std::ceil(minY)));
    const int y1 = std::min(sh-1, static_cast<int>(std::floor(maxY)));

    struct Cross { float x, invZ; };

    for (int y = y0; y <= y1; ++y) {
        const float yc = static_cast<float>(y) + 0.5f;
        std::array<Cross, 8> cr;
        int nc = 0;

        for (int i = 0; i < n; ++i) {
            const ScrVert& a = poly[i];
            const ScrVert& b = poly[(i+1)%n];
            float ax,ay,ai, bx,by,bi;
            if (a.sy <= b.sy) { ax=a.sx; ay=a.sy; ai=a.invZ; bx=b.sx; by=b.sy; bi=b.invZ; }
            else              { ax=b.sx; ay=b.sy; ai=b.invZ; bx=a.sx; by=a.sy; bi=a.invZ; }
            const float dy = by - ay;
            if (dy < 1e-6f) continue;
            if (yc >= ay && yc < by) {
                const float t = (yc - ay) / dy;
                if (nc < 8) cr[nc++] = { ax + t*(bx-ax), ai + t*(bi-ai) };
            }
        }
        if (nc < 2) continue;

        for (int i = 1; i < nc; ++i) {
            const Cross key = cr[i]; int j = i-1;
            while (j >= 0 && cr[j].x > key.x) { cr[j+1]=cr[j]; --j; }
            cr[j+1] = key;
        }
        for (int p = 0; p+1 < nc; p += 2) {
            const int xli = std::max(0,    static_cast<int>(std::ceil(cr[p].x)));
            const int xri = std::min(sw-1, static_cast<int>(std::floor(cr[p+1].x)));
            if (xli > xri) continue;
            const float span = cr[p+1].x - cr[p].x;
            const float dI   = (span > 1e-6f) ? (cr[p+1].invZ - cr[p].invZ) / span : 0.f;
            float invZ = cr[p].invZ + dI*(static_cast<float>(xli)+0.5f - cr[p].x);
            const size_t row = static_cast<size_t>(y) * sw;
            for (int x = xli; x <= xri; ++x, invZ += dI)
                if (invZ > 0.f) fb.SetPixelDepthFast(row+x, color, 1.f/invZ);
        }
    }
}

// ---------------------------------------------------------------------------
// Textured scanline fill (perspective-correct UV + Gouraud lighting).
// ---------------------------------------------------------------------------
void FillTextured(Framebuffer& fb, const std::vector<ScrVert>& poly,
                  const Texture& tex, int sw, int sh) {
    const int n = static_cast<int>(poly.size());
    if (n < 3) return;

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
            const ScrVert& a = poly[i];
            const ScrVert& b = poly[(i+1)%n];
            float ax,ay,ai,au,av,al, bx,by,bi,bu,bv,bl;
            if (a.sy <= b.sy) {
                ax=a.sx; ay=a.sy; ai=a.invZ; au=a.uOverZ; av=a.vOverZ; al=a.litOverZ;
                bx=b.sx; by=b.sy; bi=b.invZ; bu=b.uOverZ; bv=b.vOverZ; bl=b.litOverZ;
            } else {
                ax=b.sx; ay=b.sy; ai=b.invZ; au=b.uOverZ; av=b.vOverZ; al=b.litOverZ;
                bx=a.sx; by=a.sy; bi=a.invZ; bu=a.uOverZ; bv=a.vOverZ; bl=a.litOverZ;
            }
            const float dy = by - ay;
            if (dy < 1e-6f) continue;
            if (yc >= ay && yc < by) {
                const float t = (yc - ay) / dy;
                if (nc < 8) cr[nc++] = {
                    ax + t*(bx-ax), ai + t*(bi-ai),
                    au + t*(bu-au), av + t*(bv-av), al + t*(bl-al)
                };
            }
        }
        if (nc < 2) continue;

        for (int i = 1; i < nc; ++i) {
            const Cross key = cr[i]; int j = i-1;
            while (j >= 0 && cr[j].x > key.x) { cr[j+1]=cr[j]; --j; }
            cr[j+1] = key;
        }
        for (int p = 0; p+1 < nc; p += 2) {
            const float xl = cr[p].x, xr = cr[p+1].x;
            const int xli = std::max(0,    static_cast<int>(std::ceil(xl)));
            const int xri = std::min(sw-1, static_cast<int>(std::floor(xr)));
            if (xli > xri) continue;
            const float span = xr - xl;
            const float isp  = (span > 1e-6f) ? 1.f/span : 0.f;
            const float dI = (cr[p+1].invZ - cr[p].invZ) * isp;
            const float dU = (cr[p+1].uZ   - cr[p].uZ)   * isp;
            const float dV = (cr[p+1].vZ   - cr[p].vZ)   * isp;
            const float dL = (cr[p+1].litZ  - cr[p].litZ) * isp;
            const float offs = static_cast<float>(xli)+0.5f - xl;
            float invZ = cr[p].invZ + dI*offs;
            float uZ   = cr[p].uZ   + dU*offs;
            float vZ   = cr[p].vZ   + dV*offs;
            float litZ = cr[p].litZ + dL*offs;
            const size_t row = static_cast<size_t>(y) * sw;
            for (int x = xli; x <= xri; ++x, invZ+=dI, uZ+=dU, vZ+=dV, litZ+=dL) {
                if (invZ > 0.f) {
                    const float d = 1.f / invZ;
                    fb.SetPixelDepthFast(row+x, ApplyLight(tex.Sample(uZ*d, vZ*d), litZ*d), d);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers: 3D cross product and camera-space transform.
// ---------------------------------------------------------------------------
inline Vector3 Cross3(Vector3 a, Vector3 b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

std::vector<CamVert> ToCam(const std::vector<Vector3>& wv, const Camera& cam) {
    std::vector<CamVert> cv(wv.size());
    for (size_t i = 0; i < wv.size(); ++i) {
        auto v = cam.ToView(wv[i]);
        cv[i] = { v.x, v.y, v.z };
    }
    return cv;
}

// ---------------------------------------------------------------------------
// Compute flat lighting for a face from its outward normal.
// ---------------------------------------------------------------------------
float FlatLit(const Vector3& normal, const Vector3& centroid,
              const std::vector<PointLight>& lights, float ambient,
              const DirectionalLight& sun) {
    const Vector3 n = normal.Normalized();
    const Vector3 toSun = Vector3(-sun.dirX, -sun.dirY, -sun.dirZ).Normalized();
    const float lambert = std::max(0.f, Vector3::Dot(n, toSun));
    float lit = ambient + sun.intensity * lambert * 0.6f;
    for (const PointLight& pl : lights) {
        const Vector3 toL = pl.position - centroid;
        const float d2 = toL.LengthSquared();
        if (d2 >= pl.radiusSq || d2 < 1e-6f) continue;
        const float diff = std::max(0.f, Vector3::Dot(n, toL.Normalized()));
        const float t2 = d2 / pl.radiusSq;
        lit += (1.f-t2)*(1.f-t2)*pl.intensity*diff;
    }
    return lit < 1.f ? lit : 1.f;
}

} // namespace

// ---------------------------------------------------------------------------
WallRenderer::WallRenderer(int screenWidth, int screenHeight, float fov)
    : m_screenWidth(screenWidth), m_screenHeight(screenHeight) { (void)fov; }

// ---------------------------------------------------------------------------
// DrawWall — renders a solid volumetric wall as a box with 5 visible faces:
//   · Front face  — textured, Gouraud-lit (the visible interior surface)
//   · Back face   — flat-shaded (seen through open portals/doorways)
//   · Top cap     — flat-shaded, darker
//   · Left cap    — flat-shaded, slightly darker
//   · Right cap   — flat-shaded, slightly darker
// Every face uses backface culling and the depth buffer, identical to the
// MeshRenderer pipeline so walls and cubes sort correctly against each other.
// ---------------------------------------------------------------------------
void WallRenderer::DrawWall(Framebuffer& framebuffer, const Camera& camera,
                            Vector3 start, Vector3 end,
                            float bottomHeight, float topHeight,
                            const std::vector<PointLight>& lights, float ambientLight,
                            const DirectionalLight& sunLight,
                            const Texture& texture) const {
    const int sw = m_screenWidth, sh = m_screenHeight;

    // Wall tangent and length.
    const float dx = end.x - start.x;
    const float dz = end.z - start.z;
    const float len = std::sqrt(dx*dx + dz*dz);
    if (len < 1e-6f) return;

    // Outward-facing horizontal normal.  For a CCW wall start→end the outward
    // normal is (-dz, 0, dx) / len.  Flip it so it points toward the camera.
    float nx = -dz / len, nz = dx / len;
    const Vector3 mid = { (start.x+end.x)*0.5f, (bottomHeight+topHeight)*0.5f,
                           (start.z+end.z)*0.5f };
    if (nx*(camera.position.x - mid.x) + nz*(camera.position.z - mid.z) < 0.f) {
        nx = -nx; nz = -nz;
    }

    // Build the 8 corners of the box.
    const float half = kWallThickness * 0.5f;
    const float ox = nx * half, oz = nz * half;

    // Front (outward) face corners F0..F3, back B0..B3.
    const Vector3 F0 = { start.x + ox, bottomHeight, start.z + oz };
    const Vector3 F1 = { end.x   + ox, bottomHeight, end.z   + oz };
    const Vector3 F2 = { end.x   + ox, topHeight,    end.z   + oz };
    const Vector3 F3 = { start.x + ox, topHeight,    start.z + oz };
    const Vector3 B0 = { start.x - ox, bottomHeight, start.z - oz };
    const Vector3 B1 = { end.x   - ox, bottomHeight, end.z   - oz };
    const Vector3 B2 = { end.x   - ox, topHeight,    end.z   - oz };
    const Vector3 B3 = { start.x - ox, topHeight,    start.z - oz };

    // -----------------------------------------------------------------------
    // Draw textured front face with per-corner Gouraud lighting.
    // wv = { F0, F1, F2, F3 }, UVs = bottom-left→bottom-right→top-right→top-left.
    // -----------------------------------------------------------------------
    {
        const std::vector<Vector3> wv = { F0, F1, F2, F3 };
        const Vector3 normal = Cross3(wv[1]-wv[0], wv[2]-wv[0]);
        if (normal.LengthSquared() >= 1e-10f &&
            Vector3::Dot(normal, camera.position - wv[0]) > 0.f) {

            // Per-corner UV and lighting.
            const float us[4] = { 0.f, 1.f, 1.f, 0.f };
            const float vs[4] = { 1.f, 1.f, 0.f, 0.f };
            float lits[4];
            for (int i = 0; i < 4; ++i)
                lits[i] = ComputeLighting(wv[i].x, wv[i].y, wv[i].z,
                                          lights, ambientLight, sunLight);

            // Extended vertex type for textured clipping.
            struct TexVert { float x, y, z, u, v, lit; };
            std::vector<TexVert> tv(4);
            for (int i = 0; i < 4; ++i) {
                auto cv = camera.ToView(wv[i]);
                tv[i] = { cv.x, cv.y, cv.z, us[i], vs[i], lits[i] };
            }

            auto lerpTex = [](const TexVert& a, const TexVert& b, float t) -> TexVert {
                return { a.x+t*(b.x-a.x), a.y+t*(b.y-a.y), kNearPlane,
                         a.u+t*(b.u-a.u), a.v+t*(b.v-a.v), a.lit+t*(b.lit-a.lit) };
            };
            auto clipped = ClipNear(tv, lerpTex);
            if (clipped.size() >= 3) {
                std::vector<ScrVert> poly(clipped.size());
                for (size_t i = 0; i < clipped.size(); ++i) {
                    const float invZ = 1.f / clipped[i].z;
                    poly[i] = {
                        camera.halfWidth  + clipped[i].x * invZ * camera.focalLength,
                        camera.halfHeight - clipped[i].y * invZ * camera.focalLength,
                        invZ,
                        clipped[i].u   * invZ,
                        clipped[i].v   * invZ,
                        clipped[i].lit * invZ
                    };
                }
                FillTextured(framebuffer, poly, texture, sw, sh);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Lambda: draw a flat-shaded polygon face (backface-culled, lit by normal).
    // -----------------------------------------------------------------------
    auto drawFlat = [&](const std::vector<Vector3>& wv, uint32_t baseColor) {
        if (wv.size() < 3) return;

        // Compute outward normal from first two edges (CCW winding assumed).
        const Vector3 normal = Cross3(wv[1] - wv[0], wv[2] - wv[0]);
        if (normal.LengthSquared() < 1e-10f) return;

        // Backface cull: skip if camera is on the inward (wrong) side.
        if (Vector3::Dot(normal, camera.position - wv[0]) <= 0.f) return;

        // Flat lighting from the face normal (Lambert sun + point lights).
        const Vector3 centroid = (wv[0] + wv[1] + wv[2]) / 3.f;
        const float lit = FlatLit(normal, centroid, lights, ambientLight, sunLight);
        const uint32_t color = ApplyLight(baseColor, lit);

        // Transform to camera space, clip, project, fill.
        auto cv = ToCam(wv, camera);
        auto lerpCam = [](const CamVert& a, const CamVert& b, float t) -> CamVert {
            return { a.x + t*(b.x-a.x), a.y + t*(b.y-a.y), kNearPlane };
        };
        auto clipped = ClipNear(cv, lerpCam);
        if (clipped.size() < 3) return;

        std::vector<ScrVert> poly(clipped.size());
        for (size_t i = 0; i < clipped.size(); ++i)
            poly[i] = Project(clipped[i], camera);

        FillFlat(framebuffer, poly, color, sw, sh);
    };

    // Base color palette for untextured faces (depth-cued shading).
    const uint32_t kBack = Framebuffer::MakeColor(140, 118, 88);
    const uint32_t kTop  = Framebuffer::MakeColor(100,  84, 62);
    const uint32_t kCap  = Framebuffer::MakeColor(120, 100, 74);

    // Back face — CCW from outside (inward-facing): B0, B3, B2, B1
    drawFlat({ B0, B3, B2, B1 }, kBack);

    // Top cap — CCW from above: F3, F2, B2, B3
    drawFlat({ F3, F2, B2, B3 }, kTop);

    // Left cap (start side) — CCW from left side: F0, B0, B3, F3
    drawFlat({ F0, B0, B3, F3 }, kCap);

    // Right cap (end side) — CCW from right side: B1, F1, F2, B2
    drawFlat({ B1, F1, F2, B2 }, kCap);
}
