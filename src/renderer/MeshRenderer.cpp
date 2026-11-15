#include "MeshRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "Framebuffer.h"

namespace {

constexpr float kNearPlane = 0.05f;

struct ViewVertex {
    float x;
    float y;
    float z;
};

struct ScreenVertex {
    float x;
    float y;
    float invDepth;
};

struct SpanEnd {
    float x;
    float invDepth;
};

Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

// Sutherland–Hodgman clip of a triangle against the near plane (viewZ >= near).
std::vector<ViewVertex> ClipNear(const std::array<ViewVertex, 3>& in) {
    std::vector<ViewVertex> out;
    out.reserve(4);
    for (int i = 0; i < 3; ++i) {
        const ViewVertex& a = in[i];
        const ViewVertex& b = in[(i + 1) % 3];
        const bool aIn = a.z >= kNearPlane;
        const bool bIn = b.z >= kNearPlane;
        if (aIn) {
            out.push_back(a);
        }
        if (aIn != bIn) {
            const float t = (kNearPlane - a.z) / (b.z - a.z);
            out.push_back({a.x + t * (b.x - a.x),
                           a.y + t * (b.y - a.y),
                           kNearPlane});
        }
    }
    return out;
}

// Normal-aware flat shading for a face: directional sun (Lambert) plus point
// lights at the face centroid. Distinguishes a cube's faces so it reads 3D.
float ComputeMeshLighting(const Vector3& normal, const Vector3& centroid,
                          const std::vector<PointLight>& lights,
                          float ambientLight, const DirectionalLight& sun) {
    const Vector3 n = normal.Normalized();
    const Vector3 toSun = Vector3(-sun.dirX, -sun.dirY, -sun.dirZ).Normalized();
    const float lambert = std::max(0.0f, Vector3::Dot(n, toSun));
    float lit = ambientLight + sun.intensity * lambert * 0.6f;
    for (const PointLight& light : lights) {
        const Vector3 toLight = light.position - centroid;
        const float distSq = toLight.LengthSquared();
        if (distSq >= light.radiusSq || distSq < 1e-6f) { continue; }
        const Vector3 l = toLight.Normalized();
        const float diff = std::max(0.0f, Vector3::Dot(n, l));
        const float t = distSq / light.radiusSq;
        const float atten = (1.0f - t) * (1.0f - t) * light.intensity;
        lit += atten * diff;
    }
    return lit < 1.0f ? lit : 1.0f;
}

// Scanline fill of a convex polygon with perspective-correct depth.
void FillConvex(Framebuffer& framebuffer, const std::vector<ScreenVertex>& poly,
                uint32_t color, int screenWidth, int screenHeight) {
    const int n = static_cast<int>(poly.size());
    if (n < 3) { return; }

    float minY = poly[0].y, maxY = poly[0].y;
    for (int i = 1; i < n; ++i) {
        if (poly[i].y < minY) { minY = poly[i].y; }
        if (poly[i].y > maxY) { maxY = poly[i].y; }
    }
    const int y0 = std::max(0,              static_cast<int>(std::ceil(minY)));
    const int y1 = std::min(screenHeight - 1, static_cast<int>(std::ceil(maxY)) - 1);

    for (int y = y0; y <= y1; ++y) {
        const float yc = static_cast<float>(y) + 0.5f;
        std::array<SpanEnd, 8> crosses;
        int count = 0;

        for (int i = 0; i < n; ++i) {
            const int j = (i + 1 == n) ? 0 : i + 1;
            const ScreenVertex& a = poly[i];
            const ScreenVertex& b = poly[j];
            float x0, inv0, y0e, x1, inv1, y1e;
            if (a.y <= b.y) {
                x0 = a.x; inv0 = a.invDepth; y0e = a.y;
                x1 = b.x; inv1 = b.invDepth; y1e = b.y;
            } else {
                x0 = b.x; inv0 = b.invDepth; y0e = b.y;
                x1 = a.x; inv1 = a.invDepth; y1e = a.y;
            }
            const float dy = y1e - y0e;
            if (dy < 1e-6f) { continue; }
            if (yc >= y0e && yc < y1e) {
                const float t = (yc - y0e) / dy;
                if (count < 8) {
                    crosses[count].x = x0 + t * (x1 - x0);
                    crosses[count].invDepth = inv0 + t * (inv1 - inv0);
                    ++count;
                }
            }
        }
        if (count < 2) { continue; }

        for (int i = 1; i < count; ++i) {
            const SpanEnd key = crosses[i];
            int j = i - 1;
            while (j >= 0 && crosses[j].x > key.x) { crosses[j + 1] = crosses[j]; --j; }
            crosses[j + 1] = key;
        }

        for (int p = 0; p + 1 < count; p += 2) {
            const float xL = crosses[p].x;
            const float xR = crosses[p + 1].x;
            // Use half-open intervals [ceil(xL), ceil(xR)) so adjacent
            // triangles sharing an edge never leave a gap and never
            // double-draw the shared pixel (top-left fill rule).
            const int xLi = std::max(0,              static_cast<int>(std::ceil(xL)));
            const int xRi = std::min(screenWidth - 1, static_cast<int>(std::ceil(xR)) - 1);
            if (xLi > xRi) { continue; }
            const float span = xR - xL;
            const float dInv = (span > 1e-6f) ? (crosses[p + 1].invDepth - crosses[p].invDepth) / span : 0.0f;
            float inv = crosses[p].invDepth + dInv * (static_cast<float>(xLi) + 0.5f - xL);
            const size_t rowBase = static_cast<size_t>(y) * screenWidth;
            for (int x = xLi; x <= xRi; ++x, inv += dInv) {
                if (inv > 0.0f) {
                    framebuffer.SetPixelDepthFast(rowBase + x, color, 1.0f / inv);
                }
            }
        }
    }
}

Vector3 RotateY(const Vector3& v, float cosYaw, float sinYaw) {
    return {v.x * cosYaw + v.z * sinYaw,
            v.y,
            -v.x * sinYaw + v.z * cosYaw};
}

}  // namespace

Mesh MakeCube(float size) {
    const float h = size * 0.5f;
    Mesh mesh;
    mesh.vertices = {
        {-h, -h, -h}, { h, -h, -h}, { h,  h, -h}, {-h,  h, -h},  // 0..3  z = -h
        {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h},  // 4..7  z = +h
    };
    mesh.faces = {
        {4, 5, 6}, {4, 6, 7},   // +Z
        {0, 3, 2}, {0, 2, 1},   // -Z
        {1, 2, 6}, {1, 6, 5},   // +X
        {0, 7, 3}, {0, 4, 7},   // -X
        {2, 7, 6}, {2, 3, 7},   // +Y
        {0, 5, 4}, {0, 1, 5},   // -Y
    };
    return mesh;
}

MeshRenderer::MeshRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians)
    : m_screenWidth(screenWidth),
      m_screenHeight(screenHeight) {
    (void)fieldOfViewRadians;  // projection now comes from the Camera
}

void MeshRenderer::DrawMesh(Framebuffer& framebuffer, const Camera& camera,
                            const Vector3& position, float yaw, float scale,
                            const Mesh& mesh, uint32_t baseColor,
                            const std::vector<PointLight>& lights, float ambientLight,
                            const DirectionalLight& sunLight) const {
    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);

    for (const MeshTriangle& tri : mesh.faces) {
        // local → world: scale, rotate about Y, translate.
        const int idx[3] = {tri.v0, tri.v1, tri.v2};
        std::array<Vector3, 3> world;
        for (int i = 0; i < 3; ++i) {
            const Vector3 local = mesh.vertices[idx[i]] * scale;
            world[i] = position + RotateY(local, cosYaw, sinYaw);
        }

        // Outward face normal from the CCW winding.
        const Vector3 normal = Cross(world[1] - world[0], world[2] - world[0]);
        if (normal.LengthSquared() < 1e-10f) { continue; }

        // Backface cull: visible only if the camera is on the outward side of
        // the face plane (constant for any point on the plane, so world[0] ok).
        if (Vector3::Dot(normal, camera.position - world[0]) <= 0.0f) { continue; }

        // Flat shading from the face normal + sun + point lights.
        const Vector3 centroid = (world[0] + world[1] + world[2]) / 3.0f;
        const float lit = ComputeMeshLighting(normal, centroid, lights,
                                              ambientLight, sunLight);
        const uint32_t color = ApplyLight(baseColor, lit);

        // world → view space through the true 3D camera (yaw + pitch).
        std::array<ViewVertex, 3> view;
        for (int i = 0; i < 3; ++i) {
            const Camera::View vv = camera.ToView(world[i]);
            view[i].x = vv.x; view[i].y = vv.y; view[i].z = vv.z;
        }

        std::vector<ViewVertex> clipped = ClipNear(view);
        if (clipped.size() < 3) { continue; }

        // Project about the screen centre with perspective-correct inverse depth.
        std::vector<ScreenVertex> poly;
        poly.reserve(clipped.size());
        for (const ViewVertex& v : clipped) {
            const float invZ = 1.0f / v.z;
            poly.push_back({camera.halfWidth + v.x * invZ * camera.focalLength,
                            camera.halfHeight - v.y * invZ * camera.focalLength,
                            invZ});
        }

        FillConvex(framebuffer, poly, color, m_screenWidth, m_screenHeight);
    }
}

// ---------------------------------------------------------------------------
// DrawMeshOBB — renders an oriented bounding box using a full 3×3 rotation
// matrix R (column-major, R[r*3+c]).  Builds the 8 corners from halfExtents,
// rotates them into world space, then rasterises the 12 triangles (6 faces).
// ---------------------------------------------------------------------------
void MeshRenderer::DrawMeshOBB(Framebuffer& framebuffer, const Camera& camera,
                                const Vector3& position,
                                const float R[9], const Vector3& half,
                                uint32_t baseColor,
                                const std::vector<PointLight>& lights,
                                float ambientLight,
                                const DirectionalLight& sunLight) const {
    // 8 corners in local space.
    const float sx[2] = { -half.x,  half.x };
    const float sy[2] = { -half.y,  half.y };
    const float sz[2] = { -half.z,  half.z };

    // local → world: pos + R * corner.
    auto rotAdd = [&](float lx, float ly, float lz) -> Vector3 {
        return {
            position.x + R[0]*lx + R[1]*ly + R[2]*lz,
            position.y + R[3]*lx + R[4]*ly + R[5]*lz,
            position.z + R[6]*lx + R[7]*ly + R[8]*lz
        };
    };

    // Build world corners (same indexing as MakeCube).
    const Vector3 W[8] = {
        rotAdd(sx[0],sy[0],sz[0]), rotAdd(sx[1],sy[0],sz[0]),
        rotAdd(sx[1],sy[1],sz[0]), rotAdd(sx[0],sy[1],sz[0]),
        rotAdd(sx[0],sy[0],sz[1]), rotAdd(sx[1],sy[0],sz[1]),
        rotAdd(sx[1],sy[1],sz[1]), rotAdd(sx[0],sy[1],sz[1]),
    };

    // 12 triangles matching MakeCube winding (CCW outward).
    static const int kTris[12][3] = {
        {4,5,6},{4,6,7},  // +Z
        {0,3,2},{0,2,1},  // -Z
        {1,2,6},{1,6,5},  // +X
        {0,7,3},{0,4,7},  // -X (fixed winding)
        {2,7,6},{2,3,7},  // +Y
        {0,5,4},{0,1,5},  // -Y
    };

    for (const auto& t : kTris) {
        const Vector3& w0 = W[t[0]];
        const Vector3& w1 = W[t[1]];
        const Vector3& w2 = W[t[2]];

        const Vector3 normal = Cross(w1 - w0, w2 - w0);
        if (normal.LengthSquared() < 1e-10f) continue;
        if (Vector3::Dot(normal, camera.position - w0) <= 0.0f) continue;

        const Vector3 centroid = (w0 + w1 + w2) / 3.0f;
        const float lit = ComputeMeshLighting(normal, centroid, lights,
                                              ambientLight, sunLight);
        const uint32_t color = ApplyLight(baseColor, lit);

        std::array<ViewVertex, 3> view;
        for (int i = 0; i < 3; ++i) {
            const Vector3& wv = (i==0)?w0:(i==1)?w1:w2;
            const Camera::View vv = camera.ToView(wv);
            view[i] = { vv.x, vv.y, vv.z };
        }

        std::vector<ViewVertex> clipped = ClipNear(view);
        if (clipped.size() < 3) continue;

        std::vector<ScreenVertex> poly;
        poly.reserve(clipped.size());
        for (const ViewVertex& v : clipped) {
            const float invZ = 1.0f / v.z;
            poly.push_back({ camera.halfWidth  + v.x * invZ * camera.focalLength,
                             camera.halfHeight - v.y * invZ * camera.focalLength,
                             invZ });
        }
        FillConvex(framebuffer, poly, color, m_screenWidth, m_screenHeight);
    }
}
