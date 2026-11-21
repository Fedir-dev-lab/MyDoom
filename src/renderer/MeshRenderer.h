#pragma once

#include <cstdint>
#include <vector>

#include "core/Vector3.h"
#include "renderer/Camera.h"
#include "renderer/Lighting.h"

class Framebuffer;

// ---------------------------------------------------------------------------
// MeshRenderer — the engine's general polygon renderer for true 3D solids.
// Arbitrary CCW-outward triangles in world space with backface culling,
// near-plane clipping, perspective-correct depth and flat normal shading.
// Projects through the shared 3D Camera so solids stay correct under pitch.
// ---------------------------------------------------------------------------
struct MeshTriangle {
    int v0 = 0;
    int v1 = 0;
    int v2 = 0;  // indices into Mesh::vertices, CCW when viewed from outside
};

struct Mesh {
    std::vector<Vector3> vertices;      // local space, centred on the origin
    std::vector<MeshTriangle> faces;
};

// Build an axis-aligned cube of edge length `size` centred on the origin,
// as 12 CCW-outward triangles so backface culling keeps only the visible faces.
Mesh MakeCube(float size);

// Build a simplified 3D solid for medium-distance LOD 1.
// 6 vertices, 8 CCW-outward triangles (octahedron hull bounding the cube),
// providing a 33% reduction in triangles and vertex processing.
Mesh MakeSimplifiedCube(float size);

class MeshRenderer {
public:
    MeshRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians);

    void DrawMesh(Framebuffer& framebuffer, const Camera& camera,
                  const Vector3& position, float yaw, float scale,
                  const Mesh& mesh, uint32_t baseColor,
                  const std::vector<PointLight>& lights, float ambientLight,
                  const DirectionalLight& sunLight) const;

    // Full-rotation variant: R is a column-major 3×3 rotation matrix (row[i][j] = R[i*3+j]).
    void DrawMeshOBB(Framebuffer& framebuffer, const Camera& camera,
                     const Vector3& position,
                     const float R[9], const Vector3& halfExtents,
                     uint32_t baseColor,
                     const std::vector<PointLight>& lights, float ambientLight,
                     const DirectionalLight& sunLight) const;

private:
    int m_screenWidth;
    int m_screenHeight;
};
