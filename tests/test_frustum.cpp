#include <iostream>
#include <cassert>
#include <cmath>

#include "core/Vector2.h"
#include "core/Vector3.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "world/LevelData.h"

int main() {
    std::cout << "[Test] Starting MyDoom Frustum Culling Test Suite..." << std::endl;

    const int rw = 1280;
    const int rh = 720;
    const float fov = 90.0f * 3.14159265f / 180.0f;
    const float focal = (rw * 0.5f) / std::tan(fov * 0.5f);

    // 1. Camera & Frustum Build test (default and custom farDist)
    Camera cam = Camera::Make(Vector3{0.0f, 1.6f, 0.0f}, 0.0f, 0.0f, focal, rw, rh);
    Frustum fDefault;
    fDefault.Build(cam);

    Frustum fCustomFar;
    fCustomFar.Build(cam, 100.0f);

    // Object at distance 200 should be visible with default (400), but culled with custom far (100)
    assert(fDefault.TestSphere(Vector3{0.0f, 1.6f, 200.0f}, 1.0f) == true);
    assert(fCustomFar.TestSphere(Vector3{0.0f, 1.6f, 200.0f}, 1.0f) == false);
    std::cout << "  [PASS] Test 1: Frustum Build & configurable farDist" << std::endl;

    // 2. Bounding Sphere Test (front, behind, left, right, boundary crossing)
    assert(fDefault.TestSphere(Vector3{0.0f, 1.6f, 10.0f}, 0.5f) == true);
    assert(fDefault.TestSphere(Vector3{0.0f, 1.6f, -10.0f}, 0.5f) == false);
    // At z=10, half-width is 10. x=20 is culled, x=10.5 with radius 1.0 intersects and is visible
    assert(fDefault.TestSphere(Vector3{20.0f, 1.6f, 10.0f}, 1.0f) == false);
    assert(fDefault.TestSphere(Vector3{10.5f, 1.6f, 10.0f}, 1.0f) == true);
    std::cout << "  [PASS] Test 2: Bounding Sphere culling & conservatism" << std::endl;

    // 3. Tall Billboard & ExitGate Center Test
    // Tall sprite: bottom at y=0, height=4 -> center at y=2.0
    const Vector3 sprPos{0.0f, 0.0f, 5.0f};
    const float sprW = 1.0f, sprH = 4.0f;
    const Vector3 sprCenter{sprPos.x, sprPos.y + sprH * 0.5f, sprPos.z};
    const float sprR = std::sqrt((sprW * 0.5f) * (sprW * 0.5f) + (sprH * 0.5f) * (sprH * 0.5f));
    assert(fDefault.TestSphere(sprCenter, sprR) == true);

    // ExitGate (width 6, height 4)
    const float gateW = 6.0f, gateH = 4.0f;
    const Vector3 gatePos{0.0f, 0.0f, 15.0f};
    const Vector3 gateCenter{gatePos.x, gatePos.y + gateH * 0.5f, gatePos.z};
    const float gateR = std::sqrt((gateW * 0.5f) * (gateW * 0.5f) + (gateH * 0.5f) * (gateH * 0.5f));
    assert(fDefault.TestSphere(gateCenter, gateR) == true);
    std::cout << "  [PASS] Test 3: Billboard sprite center & ExitGate" << std::endl;

    // 4. Sector ComputeBounds() & Wall fallback consistency
    Sector s;
    s.floorHeight = 0.0f;
    s.ceilingHeight = std::nullopt; // open sky sector
    s.vertices = { {-5.0f, 10.0f}, {5.0f, 10.0f}, {5.0f, 20.0f}, {-5.0f, 20.0f} };
    s.walls.push_back(Wall({-5.0f, 0.0f, 10.0f}, {5.0f, 0.0f, 10.0f}, 0.0f, 5.0f, true));
    s.ComputeBounds();

    assert(s.boundsMin.x == -5.0f && s.boundsMax.x == 5.0f);
    assert(s.boundsMin.z == 10.0f && s.boundsMax.z == 20.0f);
    assert(s.boundsMin.y == 0.0f);
    assert(s.boundsMax.y >= 5.0f); // must encompass the 5.0f wall top
    std::cout << "  [PASS] Test 4: Sector ComputeBounds() wall fallback alignment" << std::endl;

    // 5. Sector Frustum Culling (in front, behind, partial)
    assert(fDefault.TestAABB(s.boundsMin, s.boundsMax) == true);

    Sector sBehind;
    sBehind.floorHeight = 0.0f;
    sBehind.ceilingHeight = 3.0f;
    sBehind.vertices = { {-5.0f, -30.0f}, {5.0f, -30.0f}, {5.0f, -10.0f}, {-5.0f, -10.0f} };
    sBehind.ComputeBounds();
    assert(fDefault.TestAABB(sBehind.boundsMin, sBehind.boundsMax) == false);
    std::cout << "  [PASS] Test 5: Sector AABB Frustum Culling" << std::endl;

    // 6. Open World Chunk Culling (50x50 chunk bounds)
    Vector3 chkFrontMin{ 0.0f, 0.0f, 50.0f };
    Vector3 chkFrontMax{ 50.0f, 6.0f, 100.0f };
    assert(fDefault.TestAABB(chkFrontMin, chkFrontMax) == true);

    Vector3 chkBehindMin{ 0.0f, 0.0f, -100.0f };
    Vector3 chkBehindMax{ 50.0f, 6.0f, -50.0f };
    assert(fDefault.TestAABB(chkBehindMin, chkBehindMax) == false);
    std::cout << "  [PASS] Test 6: Open World Chunk AABB Culling" << std::endl;

    // 7. Interactive Door Frustum Culling
    // Door segment in front at z=12
    Vector3 doorStart{-1.0f, 0.0f, 12.0f};
    Vector3 doorEnd{1.0f, 0.0f, 12.0f};
    float currentBottom = 0.0f, topHeight = 2.5f;
    Vector3 doorCenter{
        (doorStart.x + doorEnd.x) * 0.5f,
        (currentBottom + topHeight) * 0.5f,
        (doorStart.z + doorEnd.z) * 0.5f
    };
    float hdx = (doorEnd.x - doorStart.x) * 0.5f;
    float hdz = (doorEnd.z - doorStart.z) * 0.5f;
    float hdy = (topHeight - currentBottom) * 0.5f;
    float doorRadius = std::sqrt(hdx * hdx + hdy * hdy + hdz * hdz);

    assert(fDefault.TestSphere(doorCenter, doorRadius) == true);

    // Door behind camera at z=-12
    Vector3 doorBehindCenter{0.0f, 1.25f, -12.0f};
    assert(fDefault.TestSphere(doorBehindCenter, doorRadius) == false);
    std::cout << "  [PASS] Test 7: Interactive Door Culling" << std::endl;

    std::cout << "\n>>> ALL 7 FRUSTUM CULLING TESTS COMPLETED SUCCESSFULLY! <<<\n" << std::endl;
    return 0;
}
