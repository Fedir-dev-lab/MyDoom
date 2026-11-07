#include "PlaneRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "Framebuffer.h"
#include "Lighting.h"
#include "Texture.h"

namespace {
constexpr float kNearPlane = 0.05f;
}  // namespace

PlaneRenderer::PlaneRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians)
    : m_screenWidth(screenWidth),
      m_screenHeight(screenHeight),
      m_focalLength((static_cast<float>(screenWidth) * 0.5f) /
                    std::tan(fieldOfViewRadians * 0.5f)),
      m_halfWidth(static_cast<float>(screenWidth) * 0.5f) {
}

// ---------------------------------------------------------------------------
// DrawPlane — scanline rasteriser with per-row span clipping.
//
// For a horizontal plane at world height H, every screen row maps to a line in
// the world X/Z plane. With a true-pitch camera the view ray for pixel (x,y)
// has a world-Y direction component that depends only on the row:
//
//   dCamY       = (halfH - y) / focal            (up component, unit depth)
//   dWorldY     = dCamY*cosPitch - sinPitch       (independent of column x)
//   t           = (H - camY) / dWorldY            (distance along the ray)
//   depth       = t                               (= viewZ, constant per row)
//   worldX(x)   = camX + t*(right.x*dCamX + up.x*dCamY + forward.x)
//   worldZ(x)   = camZ + t*(right.z*dCamX + up.z*dCamY + forward.z)
//
// where dCamX = (x - halfW)/focal. worldX/worldZ are therefore linear in the
// screen column x, so the existing edge-crossing span math transfers directly.
//
// At pitch 0: dWorldY = dCamY, t = (H-camY)*focal/(halfH-y) and worldX0/dWorldX
// reduce to the old yaw-only formulas with horizonY = halfH — i.e. identical.
// ---------------------------------------------------------------------------
void PlaneRenderer::DrawPlane(Framebuffer& framebuffer, const Camera& cam,
                              float height, const std::vector<Vector2>& polygon,
                              const std::vector<PointLight>& lights, float ambientLight,
                              const DirectionalLight& sunLight,
                              const Texture& texture) const {
    if (polygon.size() < 3 || height == cam.position.y) {
        return;
    }

    const bool isFloor = height < cam.position.y;
    const float horizonRow = cam.HorizonRow();
    const int firstRow = std::max(0,
        isFloor ? static_cast<int>(horizonRow) + 1 : 0);
    const int lastRow  = std::min(m_screenHeight - 1,
        isFloor ? m_screenHeight - 1 : static_cast<int>(horizonRow) - 1);
    if (firstRow > lastRow) { return; }

    // AABB for point-light pre-filter.
    float minX = polygon[0].x, maxX = polygon[0].x;
    float minZ = polygon[0].y, maxZ = polygon[0].y;
    for (const Vector2& v : polygon) {
        if (v.x < minX) minX = v.x;
        if (v.x > maxX) maxX = v.x;
        if (v.y < minZ) minZ = v.y;
        if (v.y > maxZ) maxZ = v.y;
    }
    std::vector<PointLight> planeLights;
    for (const PointLight& pl : lights) {
        if (std::abs(pl.position.y - height) > pl.radius) { continue; }
        if (pl.position.x + pl.radius < minX || pl.position.x - pl.radius > maxX) { continue; }
        if (pl.position.z + pl.radius < minZ || pl.position.z - pl.radius > maxZ) { continue; }
        planeLights.push_back(pl);
    }
    const bool  isConstantLit = planeLights.empty();
    const float constantLit   = isConstantLit
        ? std::min(1.0f, ambientLight + sunLight.intensity * 0.6f)
        : ambientLight;

    const float invFocal = 1.0f / m_focalLength;
    const float halfH = static_cast<float>(m_screenHeight) * 0.5f;
    const size_t polySize = polygon.size();

    // Per-row view setup (true pitch). depth = t = viewZ.
    auto setupRow = [&](int y, float& depth,
                        float& worldX0, float& dWorldX,
                        float& worldZ0, float& dWorldZ) -> bool {
        const float dCamY = (halfH - static_cast<float>(y)) * invFocal;
        const float dWorldY = dCamY * cam.cosPitch - cam.sinPitch;
        if (std::fabs(dWorldY) < 1e-6f) { return false; }
        const float t = (height - cam.position.y) / dWorldY;
        if (t <= kNearPlane) { return false; }
        const float dCamX0 = -m_halfWidth * invFocal;          // at column 0
        worldX0 = cam.position.x + t * (cam.right.x * dCamX0 + cam.up.x * dCamY + cam.forward.x);
        dWorldX = t * cam.right.x * invFocal;
        worldZ0 = cam.position.z + t * (cam.right.z * dCamX0 + cam.up.z * dCamY + cam.forward.z);
        dWorldZ = t * cam.right.z * invFocal;
        depth = t;
        return true;
    };

    // Per-row span table (stack-allocated for the common case).
    const int rowCount = lastRow - firstRow + 1;
    constexpr int kStackRows = 1080;
    float stackMin[kStackRows], stackMax[kStackRows];
    std::vector<float> heapMin, heapMax;
    float* spanMin;
    float* spanMax;
    if (rowCount <= kStackRows) {
        spanMin = stackMin;
        spanMax = stackMax;
    } else {
        heapMin.assign(rowCount,  std::numeric_limits<float>::max());
        heapMax.assign(rowCount, -std::numeric_limits<float>::max());
        spanMin = heapMin.data();
        spanMax = heapMax.data();
    }
    for (int i = 0; i < rowCount; ++i) {
        spanMin[i] =  std::numeric_limits<float>::max();
        spanMax[i] = -std::numeric_limits<float>::max();
    }

    // Span building: for every polygon edge A→B find the screen column where
    // the row's world line crosses it.
    for (int y = firstRow; y <= lastRow; ++y) {
        float depth, worldX0, dWorldX, worldZ0, dWorldZ;
        if (!setupRow(y, depth, worldX0, dWorldX, worldZ0, dWorldZ)) {
            continue;
        }

        const int ri = y - firstRow;
        float rowMin = std::numeric_limits<float>::max();
        float rowMax = -std::numeric_limits<float>::max();

        for (size_t i = 0; i < polySize; ++i) {
            const size_t j = (i + 1 == polySize) ? 0 : i + 1;
            const Vector2& a = polygon[i];
            const Vector2& b = polygon[j];
            const float ex = b.x - a.x;
            const float ey = b.y - a.y;  // world Z delta

            const float edgeDenom = -ey * dWorldX + ex * dWorldZ;
            if (std::abs(edgeDenom) < 1e-7f) { continue; }  // parallel

            const float s = (-ey * (a.x - worldX0) + ex * (a.y - worldZ0)) / edgeDenom;

            // Verify t (position along edge) is in [0,1]. This is the only
            // validity check needed; the fill pass clamps the span to screen.
            const float t = (std::abs(ex) >= std::abs(ey))
                ? ((worldX0 + s * dWorldX) - a.x) / ex
                : ((worldZ0 + s * dWorldZ) - a.y) / ey;
            if (t < -0.001f || t > 1.001f) { continue; }

            if (s < rowMin) rowMin = s;
            if (s > rowMax) rowMax = s;
        }

        spanMin[ri] = rowMin;
        spanMax[ri] = rowMax;
    }

    // Pixel fill pass.
    for (int y = firstRow; y <= lastRow; ++y) {
        const int ri = y - firstRow;
        if (spanMin[ri] > spanMax[ri]) { continue; }

        const int firstX = std::max(0,               static_cast<int>(std::floor(spanMin[ri])));
        const int lastX  = std::min(m_screenWidth - 1, static_cast<int>(std::ceil(spanMax[ri])));
        if (firstX > lastX) { continue; }

        float depth, worldX0, dWorldX, worldZ0, dWorldZ;
        if (!setupRow(y, depth, worldX0, dWorldX, worldZ0, dWorldZ)) {
            continue;
        }

        const size_t rowBase = static_cast<size_t>(y) * m_screenWidth;
        float worldX = worldX0 + static_cast<float>(firstX) * dWorldX;
        float worldZ = worldZ0 + static_cast<float>(firstX) * dWorldZ;

        if (isConstantLit) {
            for (int x = firstX; x <= lastX; ++x, worldX += dWorldX, worldZ += dWorldZ) {
                const uint32_t texel = texture.Sample(worldX * 0.5f, worldZ * 0.5f);
                framebuffer.SetPixelDepthFast(rowBase + x, ApplyLight(texel, constantLit), depth);
            }
        } else {
            int x = firstX;
            float curLit = ComputeLighting(worldX, height, worldZ, planeLights,
                                           ambientLight, sunLight);
            while (x <= lastX) {
                const int nextX   = std::min(x + 4, lastX);
                const int spanLen = nextX - x;
                float nextLit = curLit;
                if (spanLen > 0) {
                    const float nwx = worldX + static_cast<float>(spanLen) * dWorldX;
                    const float nwz = worldZ + static_cast<float>(spanLen) * dWorldZ;
                    nextLit = ComputeLighting(nwx, height, nwz, planeLights,
                                             ambientLight, sunLight);
                }
                const float dLit = spanLen > 0
                    ? (nextLit - curLit) / static_cast<float>(spanLen)
                    : 0.0f;
                float lit = curLit;
                for (int i = 0; i < spanLen; ++i, ++x, worldX += dWorldX,
                                                        worldZ += dWorldZ, lit += dLit) {
                    const uint32_t texel = texture.Sample(worldX * 0.5f, worldZ * 0.5f);
                    framebuffer.SetPixelDepthFast(rowBase + x,
                                                  ApplyLight(texel, lit), depth);
                }
                if (x == lastX) {
                    const uint32_t texel = texture.Sample(worldX * 0.5f, worldZ * 0.5f);
                    framebuffer.SetPixelDepthFast(rowBase + x,
                                                  ApplyLight(texel, nextLit), depth);
                    break;
                }
                curLit = nextLit;
            }
        }
    }
}
