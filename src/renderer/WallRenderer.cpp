#include "WallRenderer.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Framebuffer.h"
#include "Lighting.h"
#include "Texture.h"

namespace {

constexpr float kNearPlane = 0.05f;

struct ProjectedPoint {
    float x;
    float top;
    float bottom;
    float inverseDepth;
    float uOverDepth;
};

} // namespace

WallRenderer::WallRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians)
    : m_screenWidth(screenWidth),
      m_screenHeight(screenHeight),
      m_focalLength((static_cast<float>(screenWidth) * 0.5f) /
                    std::tan(fieldOfViewRadians * 0.5f)),
      m_halfWidth(static_cast<float>(screenWidth) * 0.5f) {
}

void WallRenderer::DrawWall(Framebuffer& framebuffer,
                            const Vector3& cameraPosition, float cosAngle, float sinAngle,
                            Vector3 start, Vector3 end,
                            float bottomHeight, float topHeight,
                            float horizonY,
                            const std::vector<PointLight>& lights, float ambientLight,
                            const DirectionalLight& sunLight,
                            const Texture& texture) const {
    ViewPoint startView = ToViewSpace(start, cameraPosition, cosAngle, sinAngle);
    ViewPoint endView   = ToViewSpace(end,   cameraPosition, cosAngle, sinAngle);

    if (startView.z < kNearPlane && endView.z < kNearPlane) {
        return;
    }

    // Near-plane clipping: keeps perspective stable as a wall crosses the camera.
    if (startView.z < kNearPlane || endView.z < kNearPlane) {
        const float t = (kNearPlane - startView.z) / (endView.z - startView.z);
        const Vector3 clipped = start + (end - start) * t;
        if (startView.z < kNearPlane) {
            start     = clipped;
            startView = ToViewSpace(start, cameraPosition, cosAngle, sinAngle);
        } else {
            end     = clipped;
            endView = ToViewSpace(end, cameraPosition, cosAngle, sinAngle);
        }
    }

    const auto projectPoint = [this, &cameraPosition, bottomHeight, topHeight, horizonY](
                                  const ViewPoint& view) {
        const float screenX = m_halfWidth + (view.x / view.z) * m_focalLength;
        const float top     = horizonY - ((topHeight    - cameraPosition.y) / view.z) * m_focalLength;
        const float bottom  = horizonY - ((bottomHeight - cameraPosition.y) / view.z) * m_focalLength;
        return ProjectedPoint{screenX, top, bottom, 1.0f / view.z, 0.0f};
    };

    ProjectedPoint startScreen = projectPoint(startView);
    ProjectedPoint endScreen   = projectPoint(endView);
    endScreen.uOverDepth = endScreen.inverseDepth;

    if (startScreen.x > endScreen.x) {
        std::swap(startScreen, endScreen);
    }

    const float width = endScreen.x - startScreen.x;

    // Pre-filter point lights against wall bounding box + light radius expansion.
    std::vector<PointLight> wallLights;
    const float wallMinX = std::min(start.x, end.x);
    const float wallMaxX = std::max(start.x, end.x);
    const float wallMinZ = std::min(start.z, end.z);
    const float wallMaxZ = std::max(start.z, end.z);
    for (const PointLight& pl : lights) {
        if (pl.position.y + pl.radius < bottomHeight || pl.position.y - pl.radius > topHeight) continue;
        if (pl.position.x + pl.radius < wallMinX || pl.position.x - pl.radius > wallMaxX) continue;
        if (pl.position.z + pl.radius < wallMinZ || pl.position.z - pl.radius > wallMaxZ) continue;
        wallLights.push_back(pl);
    }

    // Degenerate single-column wall.
    if (width < 1e-6f) {
        const int x     = static_cast<int>(std::lround(startScreen.x));
        const float depth = 1.0f / startScreen.inverseDepth;
        const float span  = startScreen.bottom - startScreen.top;
        const float invSpan = (span > 0.0f) ? 1.0f / span : 0.0f;
        // Lighting once for this degenerate column at its midpoint Y.
        const float lit = ComputeLighting(start.x, (bottomHeight + topHeight) * 0.5f,
                                          start.z, wallLights, ambientLight, sunLight);
        const int firstRow = std::max(0, static_cast<int>(std::ceil(startScreen.top)));
        const int lastRow  = std::min(m_screenHeight - 1,
                                      static_cast<int>(std::floor(startScreen.bottom)));
        const int texX = texture.GetTexX(0.0f);
        float v = (static_cast<float>(firstRow) - startScreen.top) * invSpan;
        const float dV = invSpan;
        for (int y = firstRow; y <= lastRow; ++y, v += dV) {
            const size_t idx = static_cast<size_t>(y) * m_screenWidth + x;
            framebuffer.SetPixelDepthFast(idx, ApplyLight(texture.SampleColumn(texX, v), lit), depth);
        }
        return;
    }

    const float invWidth = 1.0f / width;
    // Incremental stepping replaces the per-column division t = (x - startX) / width.
    // All interpolants step linearly from start→end across [firstColumn, lastColumn].
    const float dTop          = (endScreen.top    - startScreen.top)    * invWidth;
    const float dBottom       = (endScreen.bottom - startScreen.bottom) * invWidth;
    const float dInverseDepth = (endScreen.inverseDepth - startScreen.inverseDepth) * invWidth;
    const float dUOverDepth   = (endScreen.uOverDepth   - startScreen.uOverDepth)   * invWidth;

    // Wall XZ deltas for world-position reconstruction.
    const float wallDX = end.x - start.x;
    const float wallDZ = end.z - start.z;
    // World-height span used in v → worldY mapping.
    const float heightSpan = topHeight - bottomHeight;

    const int firstColumn = std::max(0, static_cast<int>(std::ceil(startScreen.x)));
    const int lastColumn  = std::min(m_screenWidth - 1, static_cast<int>(std::floor(endScreen.x)));

    // Seed interpolants at firstColumn.
    const float tFirst       = (static_cast<float>(firstColumn) - startScreen.x) * invWidth;
    float colTop          = startScreen.top          + dTop          * (static_cast<float>(firstColumn) - startScreen.x);
    float colBottom       = startScreen.bottom       + dBottom       * (static_cast<float>(firstColumn) - startScreen.x);
    float colInverseDepth = startScreen.inverseDepth + dInverseDepth * (static_cast<float>(firstColumn) - startScreen.x);
    float colUOverDepth   = startScreen.uOverDepth   + dUOverDepth   * (static_cast<float>(firstColumn) - startScreen.x);
    (void)tFirst; // used implicitly via the expressions above

    for (int x = firstColumn; x <= lastColumn; ++x,
             colTop          += dTop,
             colBottom       += dBottom,
             colInverseDepth += dInverseDepth,
             colUOverDepth   += dUOverDepth) {

        const float depth = 1.0f / colInverseDepth;
        const float u     = colUOverDepth * depth;   // perspective-correct u

        // World XZ for this column — used for lighting and world-position.
        const float worldX = start.x + wallDX * u;
        const float worldZ = start.z + wallDZ * u;

        // --- Lighting computed ONCE per column (not per pixel) ---
        const float midWorldY = bottomHeight + heightSpan * 0.5f;
        const float lit = ComputeLighting(worldX, midWorldY, worldZ, wallLights, ambientLight, sunLight);

        const int texX = texture.GetTexX(u);
        const float span    = colBottom - colTop;
        const float invSpan = (span > 0.0f) ? 1.0f / span : 0.0f;

        const int firstRow = std::max(0, static_cast<int>(std::ceil(colTop)));
        const int lastRow  = std::min(m_screenHeight - 1, static_cast<int>(std::floor(colBottom)));

        const size_t rowBase = static_cast<size_t>(firstRow) * m_screenWidth;
        size_t idx = rowBase + x;

        float v = (static_cast<float>(firstRow) - colTop) * invSpan;
        const float dV = invSpan;

        for (int y = firstRow; y <= lastRow; ++y, idx += m_screenWidth, v += dV) {
            framebuffer.SetPixelDepthFast(idx, ApplyLight(texture.SampleColumn(texX, v), lit), depth);
        }
    }
}

WallRenderer::ViewPoint WallRenderer::ToViewSpace(const Vector3& point,
                                                   const Vector3& cameraPosition,
                                                   float cosAngle, float sinAngle) const {
    const float relX = point.x - cameraPosition.x;
    const float relZ = point.z - cameraPosition.z;
    return {
        relX * cosAngle - relZ * sinAngle,
        relX * sinAngle + relZ * cosAngle,
    };
}
