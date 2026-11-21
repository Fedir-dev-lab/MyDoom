#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

#include "core/Vector3.h"
#include "renderer/LODSystem.h"
#include "renderer/MeshRenderer.h"
#include "renderer/Texture.h"
#include "renderer/Framebuffer.h"
#include "renderer/Frustum.h"
#include "renderer/Camera.h"

int main() {
    std::cout << "[Test] Starting MyDoom Image Impostor LOD Test Suite..." << std::endl;

    LODConfig config;
    config.lod0To1Distance = 25.0f;
    config.lod1To2Distance = 70.0f;
    config.maxDistance     = 200.0f;
    config.hysteresis      = 3.0f;
    config.enabled         = true;

    LODSystem lod(config);

    // Dummy meshes and textures for testing
    Mesh dummyFull = MakeCube(1.0f);
    Mesh dummySimplified = MakeSimplifiedCube(1.0f);
    Texture dummyTex(16, 16, std::vector<uint32_t>(16 * 16, 0xFFFFFFFF));

    LODModel standardModel;
    standardModel.fullMesh = &dummyFull;
    standardModel.simplifiedMesh = &dummySimplified;
    standardModel.impostorTexture = &dummyTex;

    // -----------------------------------------------------------------------
    // Test 1: Distance-based Tier Selection (provisional defaults)
    // -----------------------------------------------------------------------
    {
        // Nearby object: distance 10.0f -> LOD 0 (Full)
        LODTier t0 = lod.SelectTier(10.0f, LODTier::LOD0_Full, true, &standardModel);
        assert(t0 == LODTier::LOD0_Full);

        // Medium distance: distance 45.0f -> LOD 1 (Simplified)
        LODTier t1 = lod.SelectTier(45.0f, LODTier::LOD0_Full, true, &standardModel);
        assert(t1 == LODTier::LOD1_Simplified);

        // Far distance: distance 100.0f -> LOD 2 (Impostor)
        LODTier t2 = lod.SelectTier(100.0f, LODTier::LOD0_Full, true, &standardModel);
        assert(t2 == LODTier::LOD2_Impostor);

        // Beyond max distance: distance 250.0f -> Culled
        LODTier t3 = lod.SelectTier(250.0f, LODTier::LOD0_Full, true, &standardModel);
        assert(t3 == LODTier::Culled);

        std::cout << "  [PASS] Test 1: Distance-based tier selection (LOD0, LOD1, LOD2, Culled)" << std::endl;
    }

    // -----------------------------------------------------------------------
    // Test 2: Hysteresis Stability (anti-flickering near boundaries)
    // -----------------------------------------------------------------------
    {
        // Boundary between LOD 0 and LOD 1 is 25.0f with hysteresis 3.0f
        // Moving away: from LOD 0, transition to LOD 1 only when dist > 25 + 3 = 28.0f
        LODTier state = LODTier::LOD0_Full;

        state = lod.SelectTier(24.0f, state, true, &standardModel);
        assert(state == LODTier::LOD0_Full);

        state = lod.SelectTier(26.0f, state, true, &standardModel); // inside hysteresis zone
        assert(state == LODTier::LOD0_Full);

        state = lod.SelectTier(29.0f, state, true, &standardModel); // crossed threshold + hyst
        assert(state == LODTier::LOD1_Simplified);

        // Moving closer: from LOD 1, transition back to LOD 0 only when dist < 25 - 3 = 22.0f
        state = lod.SelectTier(27.0f, state, true, &standardModel);
        assert(state == LODTier::LOD1_Simplified);

        state = lod.SelectTier(24.0f, state, true, &standardModel); // inside hysteresis zone
        assert(state == LODTier::LOD1_Simplified);

        state = lod.SelectTier(21.0f, state, true, &standardModel); // crossed threshold - hyst
        assert(state == LODTier::LOD0_Full);

        // Boundary between LOD 1 and LOD 2 (70.0f with hysteresis 3.0f)
        state = LODTier::LOD1_Simplified;
        state = lod.SelectTier(71.0f, state, true, &standardModel);
        assert(state == LODTier::LOD1_Simplified); // <= 73.0f stays LOD 1

        state = lod.SelectTier(74.0f, state, true, &standardModel);
        assert(state == LODTier::LOD2_Impostor); // > 73.0f switches to Impostor

        state = lod.SelectTier(69.0f, state, true, &standardModel);
        assert(state == LODTier::LOD2_Impostor); // >= 67.0f stays Impostor

        state = lod.SelectTier(66.0f, state, true, &standardModel);
        assert(state == LODTier::LOD1_Simplified); // < 67.0f switches back to Simplified

        std::cout << "  [PASS] Test 2: Hysteresis stability prevents boundary flickering" << std::endl;
    }

    // -----------------------------------------------------------------------
    // Test 3: Asset Fallback Policies (missing simplified mesh or impostor)
    // -----------------------------------------------------------------------
    {
        // Case A: Missing simplified mesh (simplifiedMesh == nullptr)
        // At medium distance (45.0f), should fall back to LOD 0 (Full mesh)
        LODModel noSimplifiedModel;
        noSimplifiedModel.fullMesh = &dummyFull;
        noSimplifiedModel.simplifiedMesh = nullptr;
        noSimplifiedModel.impostorTexture = &dummyTex;

        LODTier tierA = lod.SelectTier(45.0f, LODTier::LOD0_Full, true, &noSimplifiedModel);
        assert(tierA == LODTier::LOD0_Full);

        // Case B: Missing impostor texture (impostorTexture == nullptr)
        // At far distance (100.0f), should fall back to LOD 1 (Simplified mesh)
        LODModel noImpostorModel;
        noImpostorModel.fullMesh = &dummyFull;
        noImpostorModel.simplifiedMesh = &dummySimplified;
        noImpostorModel.impostorTexture = nullptr;

        LODTier tierB = lod.SelectTier(100.0f, LODTier::LOD0_Full, true, &noImpostorModel);
        assert(tierB == LODTier::LOD1_Simplified);

        // Case C: Missing both simplified mesh and impostor texture
        // At far distance, should fall back to LOD 0 (Full mesh)
        LODModel fullOnlyModel;
        fullOnlyModel.fullMesh = &dummyFull;
        fullOnlyModel.simplifiedMesh = nullptr;
        fullOnlyModel.impostorTexture = nullptr;

        LODTier tierC = lod.SelectTier(100.0f, LODTier::LOD0_Full, true, &fullOnlyModel);
        assert(tierC == LODTier::LOD0_Full);

        std::cout << "  [PASS] Test 3: Asset fallback policies (predictable and crash-free)" << std::endl;
    }

    // -----------------------------------------------------------------------
    // Test 4: Per-model overrides and Opt-outs
    // -----------------------------------------------------------------------
    {
        // Model with allowLOD = false: must always return LOD 0
        LODModel noLODModel = standardModel;
        noLODModel.allowLOD = false;
        assert(lod.SelectTier(100.0f, LODTier::LOD0_Full, true, &noLODModel) == LODTier::LOD0_Full);

        // Object with enableLOD parameter = false
        assert(lod.SelectTier(100.0f, LODTier::LOD0_Full, false, &standardModel) == LODTier::LOD0_Full);

        // Master toggle config.enabled = false
        LODConfig disabledConfig = config;
        disabledConfig.enabled = false;
        LODSystem disabledSystem(disabledConfig);
        assert(disabledSystem.SelectTier(100.0f, LODTier::LOD0_Full, true, &standardModel) == LODTier::LOD0_Full);

        // Model with custom distance thresholds
        LODModel customDistModel = standardModel;
        customDistModel.distanceLOD1 = 10.0f; // early switch
        customDistModel.distanceLOD2 = 30.0f;
        assert(lod.SelectTier(15.0f, LODTier::LOD0_Full, true, &customDistModel) == LODTier::LOD1_Simplified);
        assert(lod.SelectTier(35.0f, LODTier::LOD0_Full, true, &customDistModel) == LODTier::LOD2_Impostor);

        std::cout << "  [PASS] Test 4: Per-model overrides and opt-out flags" << std::endl;
    }

    // -----------------------------------------------------------------------
    // Test 5: Multi-view Impostor Angle Selection (future-ready extension)
    // -----------------------------------------------------------------------
    {
        Texture view0(8, 8, std::vector<uint32_t>(64, 0xFF0000FF));
        Texture view1(8, 8, std::vector<uint32_t>(64, 0x00FF00FF));
        Texture view2(8, 8, std::vector<uint32_t>(64, 0x0000FFFF));
        Texture view3(8, 8, std::vector<uint32_t>(64, 0xFFFF00FF));

        LODModel multiViewModel = standardModel;
        multiViewModel.multiViewImpostors = { &view0, &view1, &view2, &view3 };

        constexpr float kPi = 3.14159265f;
        // 4 directions: 0 -> view0, pi/2 (1.57) -> view1, pi (3.14) -> view2, 3pi/2 (4.71) -> view3
        assert(multiViewModel.GetImpostorForAngle(0.0f) == &view0);
        assert(multiViewModel.GetImpostorForAngle(kPi * 0.5f) == &view1);
        assert(multiViewModel.GetImpostorForAngle(kPi) == &view2);
        assert(multiViewModel.GetImpostorForAngle(kPi * 1.5f) == &view3);
        // Wraparound check: 2pi + 0.1 -> view0
        assert(multiViewModel.GetImpostorForAngle(kPi * 2.0f + 0.1f) == &view0);

        std::cout << "  [PASS] Test 5: Multi-view directional impostor selection" << std::endl;
    }

    // -----------------------------------------------------------------------
    // Test 6: Frustum Culling + LOD Integration Order
    // -----------------------------------------------------------------------
    {
        const int rw = 1280, rh = 720;
        const float fov = 90.0f * 3.14159265f / 180.0f;
        const float focal = (rw * 0.5f) / std::tan(fov * 0.5f);
        Camera cam = Camera::Make(Vector3{0.0f, 1.6f, 0.0f}, 0.0f, 0.0f, focal, rw, rh);
        Frustum frustum;
        frustum.Build(cam);

        // Object behind camera (z = -10.0f)
        Vector3 behindPos{0.0f, 1.6f, -10.0f};
        float radius = 1.0f;

        // Step 1: Frustum Culling MUST be performed first
        bool visible = frustum.TestSphere(behindPos, radius);
        assert(visible == false);

        // In the engine, if visible == false, LOD evaluation is SKIPPED:
        // if (!SphereVisible(pos, radius)) continue;
        // Verify that only surviving objects evaluate LOD
        Vector3 frontPos{0.0f, 1.6f, 50.0f};
        bool frontVisible = frustum.TestSphere(frontPos, radius);
        assert(frontVisible == true);

        float dist = cam.position.DistanceTo(frontPos);
        LODTier frontTier = lod.SelectTier(dist, LODTier::LOD0_Full, true, &standardModel);
        assert(frontTier == LODTier::LOD1_Simplified);

        std::cout << "  [PASS] Test 6: Frustum Culling precedes LOD evaluation" << std::endl;
    }

    // -----------------------------------------------------------------------
    // Test 7: Simplified Geometry and Impostor Texture Validity
    // -----------------------------------------------------------------------
    {
        Mesh full = MakeCube(2.0f);
        Mesh simp = MakeSimplifiedCube(2.0f);

        // Full cube: 12 triangles (faces)
        assert(full.faces.size() == 12);
        // Simplified cube (octahedron hull): 8 triangles (faces) - 33% reduction
        assert(simp.faces.size() == 8);

        // Impostor texture generation
        Texture impTex = Texture::CreateCubeImpostor(64, 64,
            Framebuffer::MakeColor(220, 180, 140),
            Framebuffer::MakeColor(170, 130, 90),
            Framebuffer::MakeColor(130, 95, 60));
        assert(impTex.Width() == 64);
        assert(impTex.Height() == 64);

        // Verify transparency in outer corner (alpha channel == 0) and opacity in center (alpha == 255)
        const uint32_t cornerTexel = impTex.Sample(0.01f, 0.01f);
        const uint32_t centerTexel = impTex.Sample(0.5f, 0.5f);
        assert((cornerTexel & 0xFF) == 0x00); // transparent background
        assert((centerTexel & 0xFF) == 0xFF); // solid center face

        std::cout << "  [PASS] Test 7: Simplified mesh and impostor texture valid" << std::endl;
    }

    std::cout << "\n[ALL TESTS PASSED] MyDoom Image Impostor LOD system verified successfully!" << std::endl;
    return 0;
}
