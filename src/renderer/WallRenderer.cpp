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
// Helpers: 3D cross product.
// ---------------------------------------------------------------------------
inline Vector3 Cross3(const Vector3& a, const Vector3& b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

} // namespace

// ---------------------------------------------------------------------------
WallRenderer::WallRenderer(int screenWidth, int screenHeight, float fov)
    : m_screenWidth(screenWidth), m_screenHeight(screenHeight) { (void)fov; }

// ---------------------------------------------------------------------------
// DrawWall — renders a solid volumetric wall as a box with 6 textured faces:
//   · Front face  — textured, Gouraud-lit (the visible surface facing outward)
//   · Back face   — textured (seen from the opposite side)
//   · Top cap     — textured (seen from above)
//   · Bottom cap  — textured (seen from below)
//   · Left cap    — textured (start end of wall)
//   · Right cap   — textured (end end of wall)
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

    // Fixed outward normal from the wall's winding order (start→end, CCW).
    // Do NOT flip toward the camera — a volumetric wall is a fixed 3D box.
    // The front face faces one side, the back face faces the other, and
    // backface culling naturally shows whichever face the camera is on.
    const float nx = -dz / len, nz = dx / len;

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
    // Lambda: draw a textured polygon face with per-corner UV + Gouraud lighting.
    // wv = world corners (CCW from outside), uvs = {u0,v0, u1,v1, ...}.
    // -----------------------------------------------------------------------
    struct TexVert { float x, y, z, u, v, lit; };

    auto drawTexFace = [&](const std::vector<Vector3>& wv, const float* uvs) {
        if (wv.size() < 3) return;

        // Outward normal via cross product of first two edges.
        const Vector3 normal = Cross3(wv[1]-wv[0], wv[2]-wv[0]);
        if (normal.LengthSquared() < 1e-10f) return;
        // Backface cull.
        if (Vector3::Dot(normal, camera.position - wv[0]) <= 0.f) return;

        // Per-corner Gouraud lighting.
        std::vector<TexVert> tv(wv.size());
        for (size_t i = 0; i < wv.size(); ++i) {
            auto cv = camera.ToView(wv[i]);
            const float lit = ComputeLighting(wv[i].x, wv[i].y, wv[i].z,
                                              lights, ambientLight, sunLight);
            tv[i] = { cv.x, cv.y, cv.z, uvs[i*2], uvs[i*2+1], lit };
        }

        auto lerpTex = [](const TexVert& a, const TexVert& b, float t) -> TexVert {
            return { a.x+t*(b.x-a.x), a.y+t*(b.y-a.y), kNearPlane,
                     a.u+t*(b.u-a.u), a.v+t*(b.v-a.v), a.lit+t*(b.lit-a.lit) };
        };
        auto clipped = ClipNear(tv, lerpTex);
        if (clipped.size() < 3) return;

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
    };

    // -----------------------------------------------------------------------
    // Draw all 5 textured faces.
    // -----------------------------------------------------------------------

    // Front face (main visible surface): F0→F1→F2→F3
    // UV: u along wall length (0→1), v bottom(1)→top(0)
    {
        const std::vector<Vector3> wv = { F0, F1, F2, F3 };
        const float uvs[8] = { 0.f,1.f,  1.f,1.f,  1.f,0.f,  0.f,0.f };
        drawTexFace(wv, uvs);
    }

    // Back face (inward-facing): B0→B3→B2→B1
    // Mirror UV horizontally so texture reads same direction
    {
        const std::vector<Vector3> wv = { B0, B3, B2, B1 };
        const float uvs[8] = { 1.f,1.f,  1.f,0.f,  0.f,0.f,  0.f,1.f };
        drawTexFace(wv, uvs);
    }

    // Top cap: F3→F2→B2→B3
    // Normal points up (+Y). UV mapped across the narrow thickness.
    {
        const std::vector<Vector3> wv = { F3, F2, B2, B3 };
        const float uvs[8] = { 0.f,0.f,  1.f,0.f,  1.f,1.f,  0.f,1.f };
        drawTexFace(wv, uvs);
    }

    // Bottom cap: F0→B0→B1→F1
    // Normal points down (-Y). Same UV layout as top.
    {
        const std::vector<Vector3> wv = { F0, B0, B1, F1 };
        const float uvs[8] = { 0.f,0.f,  1.f,0.f,  1.f,1.f,  0.f,1.f };
        drawTexFace(wv, uvs);
    }

    // Left cap (start side) — CCW from outside: F0, F3, B3, B0
    // Normal points toward start (away from wall) — verified by cross product.
    {
        const std::vector<Vector3> wv = { F0, F3, B3, B0 };
        const float uvs[8] = { 0.f,1.f,  0.f,0.f,  1.f,0.f,  1.f,1.f };
        drawTexFace(wv, uvs);
    }

    // Right cap (end side) — CCW from outside: B1, B2, F2, F1
    // Normal points toward end (away from wall) — verified by cross product.
    {
        const std::vector<Vector3> wv = { B1, B2, F2, F1 };
        const float uvs[8] = { 0.f,1.f,  0.f,0.f,  1.f,0.f,  1.f,1.f };
        drawTexFace(wv, uvs);
    }
}
