#include "PlaneRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "Framebuffer.h"
#include "Lighting.h"
#include "Texture.h"

PlaneRenderer::PlaneRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians)
    : m_screenWidth(screenWidth),
      m_screenHeight(screenHeight),
      m_focalLength((static_cast<float>(screenWidth) * 0.5f) /
                    std::tan(fieldOfViewRadians * 0.5f)),
      m_halfWidth(static_cast<float>(screenWidth) * 0.5f) {
}

// ---------------------------------------------------------------------------
// DrawPlane — scanline rasteriser with per-row span clipping
//
// For each screen row we need the range of screen columns [spanMin, spanMax]
// that falls inside the sector polygon.  The correct way to find this is to
// intersect each polygon edge with the scanline's world ray and collect the
// resulting screen-column crossings.
//
// Per-row setup (done once per row, shared by all edges):
//   viewZ    = (height - camY) * focalLength / (horizonY - y)
//   rowScale = viewZ / focalLength
//   worldX(x)= camX + (x - halfW) * rowScale * cosAngle + viewZ * sinAngle
//   worldZ(x)= camZ - (x - halfW) * rowScale * sinAngle + viewZ * cosAngle
//
// Inverting worldX/worldZ for the screen column x is exact and cheap:
//   camRelX = relX * cosAngle - relZ * sinAngle   (right component)
//   x = halfW + camRelX * focalLength / viewZ
//
// For each polygon edge (A→B) we solve for the screen column where the
// edge crosses this row's world ray plane, using the parametric
// line–line intersection in world X/Z:
//
//   P(t) = A + t*(B-A)   (edge, t∈[0,1])
//   Q(s) = origin + s*dir  (ray direction along the scanline)
//
// Then x = halfW + camX(P(t)) * focalLength / viewZ.
//
// This gives exactly one crossing per edge per row (or none if parallel).
// Collecting all crossings and sorting them gives the correct fill span,
// correctly handling concave polygons and all camera angles.
// ---------------------------------------------------------------------------

void PlaneRenderer::DrawPlane(Framebuffer& framebuffer, const Vector3& cameraPosition,
                              float cosAngle, float sinAngle,
                              float height,
                              const std::vector<Vector2>& polygon,
                              float horizonY,
                              const std::vector<PointLight>& lights, float ambientLight,
                              const DirectionalLight& sunLight,
                              const Texture& texture) const {
    if (polygon.size() < 3 || height == cameraPosition.y) {
        return;
    }

    const bool isFloor = height < cameraPosition.y;
    const int firstRow = std::max(0,
        isFloor ? static_cast<int>(horizonY) + 1 : 0);
    const int lastRow  = std::min(m_screenHeight - 1,
        isFloor ? m_screenHeight - 1 : static_cast<int>(horizonY) - 1);

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

    const float heightDiffFocal = (height - cameraPosition.y) * m_focalLength;
    const size_t polySize = polygon.size();

    // Pre-project all polygon vertices into camera-relative X (right component)
    // once — this part does not depend on viewZ so it is constant per DrawPlane.
    // camRelX[i] = (polygon[i].x - camX)*cosAngle - (polygon[i].y - camZ)*sinAngle
    std::vector<float> camRelX(polySize);
    for (size_t i = 0; i < polySize; ++i) {
        const float rx = polygon[i].x - cameraPosition.x;
        const float rz = polygon[i].y - cameraPosition.z;
        camRelX[i] = rx * cosAngle - rz * sinAngle;
    }

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

    // -----------------------------------------------------------------------
    // Span building: for every polygon edge A→B find the exact screen column
    // where it crosses each scanline row.
    //
    // The world ray for row y is parameterised by screen column x:
    //   worldX(x) = camX + (x - halfW) * rowScale * cosAngle + viewZ * sinAngle
    //   worldZ(x) = camZ - (x - halfW) * rowScale * sinAngle + viewZ * cosAngle
    //
    // The edge is: P(t) = A + t*(B-A), t∈[0,1].
    //
    // The camera-space X of a world point (wx,wz) is:
    //   camX(wx,wz) = (wx-camX)*cosAngle - (wz-camZ)*sinAngle
    //
    // By linearity:
    //   camX(P(t)) = camRelX[A] + t*(camRelX[B]-camRelX[A])
    //
    // This is a linear function of t and does NOT depend on viewZ (the
    // forward component cancels).  The screen column of the crossing is:
    //   screenX(t) = halfW + camX(P(t)) * focalLength / viewZ
    //
    // We need the value of t where the scanline ray passes through the edge,
    // i.e. we need P(t) to lie on the scanline.  But ANY t∈[0,1] gives a
    // valid edge crossing — we just need ONE crossing per edge per row.
    //
    // The approach: for each row, compute screenX at t=0 and t=1 (the two
    // projected vertices), then find whether the scanline's world-Z window
    // [worldZmin, worldZmax] intersects the edge's world-Z range, and if so
    // use linear interpolation in world-Z to find the crossing t, then
    // compute its screen column.
    //
    // World-Z of P(t): wz(t) = A.z + t*(B.z-A.z)
    // The scanline's world-Z range is not a single value — it spans the
    // entire visible row.  So we cannot clip by Z directly.
    //
    // CORRECT approach: treat each polygon edge as a line segment in world
    // X/Z and find the screen column of its intersection with the world ray
    // that corresponds to each screen row, exactly as the original
    // IsPointInPolygon + edge scan did.
    //
    // The original code solved this correctly via:
    //   s = (edgeDeltaX*(Aw-worldX0) - edgeDeltaZ*(Az-worldZ0)) / edgeDenom
    // where s is the screen-column offset from x=0.  We replicate that here,
    // but precompute the per-row (worldX0, dWorldX, worldZ0, dWorldZ) once
    // and reuse them for all edges on that row.
    // -----------------------------------------------------------------------
    for (int y = firstRow; y <= lastRow; ++y) {
        const float denom = horizonY - static_cast<float>(y);
        if (denom == 0.0f) { continue; }
        const float viewZ = heightDiffFocal / denom;
        if (viewZ <= 0.0f) { continue; }

        const float rowScale = viewZ / m_focalLength;
        // World position at screen column 0:
        const float worldX0 = cameraPosition.x
                              + (-m_halfWidth) * rowScale * cosAngle
                              + viewZ * sinAngle;
        const float worldZ0 = cameraPosition.z
                              - (-m_halfWidth) * rowScale * sinAngle
                              + viewZ * cosAngle;
        // World-space step per screen column:
        const float dWorldX = rowScale * cosAngle;
        const float dWorldZ = -rowScale * sinAngle;

        const int ri = y - firstRow;
        float rowMin = std::numeric_limits<float>::max();
        float rowMax = -std::numeric_limits<float>::max();

        for (size_t i = 0; i < polySize; ++i) {
            const size_t j = (i + 1 == polySize) ? 0 : i + 1;
            const Vector2& a = polygon[i];
            const Vector2& b = polygon[j];
            const float ex = b.x - a.x;
            const float ey = b.y - a.y; // world Z delta

            // edgeDenom = cross(dWorld, edge) in X/Z plane
            const float edgeDenom = -ey * dWorldX + ex * dWorldZ;
            if (std::abs(edgeDenom) < 1e-7f) { continue; } // parallel

            // Parameter s = screen column offset from 0 where ray hits edge
            const float s = (-ey * (a.x - worldX0) + ex * (a.y - worldZ0)) / edgeDenom;

            // Verify t (position along edge) is in [0,1]. This is the only
            // validity check we need: it guarantees the crossing lies on the
            // edge segment, which in turn bounds it to the polygon's extent.
            // We deliberately do NOT clip s to the screen here — discarding
            // off-screen crossings would empty the span whenever the polygon
            // is wider than the screen (i.e. for every row closer than the
            // far wall), making the floor/ceiling vanish. The fill pass
            // clamps the final span to the screen bounds.
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

    // -----------------------------------------------------------------------
    // Pixel fill pass (unchanged from original).
    // -----------------------------------------------------------------------
    for (int y = firstRow; y <= lastRow; ++y) {
        const int ri = y - firstRow;
        if (spanMin[ri] > spanMax[ri]) { continue; }

        const int firstX = std::max(0,               static_cast<int>(std::floor(spanMin[ri])));
        const int lastX  = std::min(m_screenWidth - 1, static_cast<int>(std::ceil(spanMax[ri])));
        if (firstX > lastX) { continue; }

        const float denom = horizonY - static_cast<float>(y);
        if (denom == 0.0f) { continue; }
        const float viewZ = heightDiffFocal / denom;
        if (viewZ <= 0.0f) { continue; }

        const float rowScale = viewZ / m_focalLength;
        const float worldX0 = cameraPosition.x
                              + (-m_halfWidth) * rowScale * cosAngle
                              + viewZ * sinAngle;
        const float worldZ0 = cameraPosition.z
                              - (-m_halfWidth) * rowScale * sinAngle
                              + viewZ * cosAngle;
        const float dWorldX = rowScale * cosAngle;
        const float dWorldZ = -rowScale * sinAngle;

        const size_t rowBase = static_cast<size_t>(y) * m_screenWidth;
        float worldX = worldX0 + static_cast<float>(firstX) * dWorldX;
        float worldZ = worldZ0 + static_cast<float>(firstX) * dWorldZ;

        if (isConstantLit) {
            for (int x = firstX; x <= lastX; ++x, worldX += dWorldX, worldZ += dWorldZ) {
                const uint32_t texel = texture.Sample(worldX * 0.5f, worldZ * 0.5f);
                framebuffer.SetPixelDepthFast(rowBase + x, ApplyLight(texel, constantLit), viewZ);
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
                                                  ApplyLight(texel, lit), viewZ);
                }
                if (x == lastX) {
                    const uint32_t texel = texture.Sample(worldX * 0.5f, worldZ * 0.5f);
                    framebuffer.SetPixelDepthFast(rowBase + x,
                                                  ApplyLight(texel, nextLit), viewZ);
                    break;
                }
                curLit = nextLit;
            }
        }
    }
}
