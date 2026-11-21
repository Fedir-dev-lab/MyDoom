#pragma once

// ---------------------------------------------------------------------------
// LODSystem — Distance-based Level of Detail and Image Impostor System.
//
// Tiers:
//   · LOD 0 — Full geometry (detailed 3D mesh)
//   · LOD 1 — Simplified geometry (reduced triangle count 3D mesh)
//   · LOD 2 — Impostor (camera-facing billboard textured image)
//   · Culled — Beyond maximum render distance
//
// Features:
//   · Configurable distance thresholds and boundary hysteresis
//   · Predictable fallbacks if simplified meshes or impostor textures are missing
//   · Extensible for multi-view impostors (camera azimuth angle selection)
//   · Zero per-frame heap allocations; pure arithmetic
// ---------------------------------------------------------------------------

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/Vector3.h"

// Forward declarations
struct Mesh;
class Texture;

enum class LODTier : uint8_t {
    LOD0_Full = 0,       // Full 3D geometry
    LOD1_Simplified = 1, // Simplified 3D geometry
    LOD2_Impostor = 2,   // Image impostor billboard
    Culled = 3           // Beyond maximum render distance
};

// Global distance thresholds and transition configuration
struct LODConfig {
    float lod0To1Distance = 25.0f; // Distance to switch from Full to Simplified
    float lod1To2Distance = 70.0f; // Distance to switch from Simplified to Impostor
    float maxDistance     = 200.0f;// Distance cutoff where object is omitted
    float hysteresis      = 3.0f;  // Buffer zone preventing boundary flickering
    bool  enabled         = true;  // Master LOD toggle
};

// Association between 3D geometry representations, simplified models, and impostor images
struct LODModel {
    const Mesh* fullMesh = nullptr;              // Full geometry (LOD 0)
    const Mesh* simplifiedMesh = nullptr;        // Simplified geometry (LOD 1)
    const Texture* impostorTexture = nullptr;    // Single-view impostor (LOD 2)

    // Future-ready: multi-view impostor texture slices (e.g. 8 or 16 views)
    std::vector<const Texture*> multiViewImpostors;

    // Impostor billboard sizing & base offset
    float impostorWidth  = 1.0f;
    float impostorHeight = 1.0f;
    Vector3 baseOffset{0.0f, 0.0f, 0.0f};

    // Optional per-model distance overrides (nullopt = use global LODConfig)
    std::optional<float> distanceLOD1;
    std::optional<float> distanceLOD2;
    std::optional<float> distanceCull;

    bool allowLOD      = true;
    bool allowImpostor = true;

    // Selects the appropriate impostor view texture based on view angle in radians
    const Texture* GetImpostorForAngle(float viewAngleRadians = 0.0f) const {
        if (multiViewImpostors.empty()) {
            return impostorTexture;
        }
        constexpr float kTwoPi = 6.28318530718f;
        float norm = std::fmod(viewAngleRadians, kTwoPi);
        if (norm < 0.0f) norm += kTwoPi;
        const size_t n = multiViewImpostors.size();
        const size_t index = static_cast<size_t>((norm / kTwoPi) * n + 0.5f) % n;
        return multiViewImpostors[index] ? multiViewImpostors[index] : impostorTexture;
    }
};

class LODSystem {
public:
    explicit LODSystem(LODConfig config = {}) : m_config(config) {}

    LODConfig& Config() { return m_config; }
    const LODConfig& Config() const { return m_config; }

    // Evaluates the representation tier for an object given its distance to the camera,
    // its current tier (for hysteresis stability), and an optional model descriptor (for fallbacks).
    LODTier SelectTier(float distance, LODTier currentTier, bool enableLOD = true,
                       const LODModel* model = nullptr) const {
        if (!m_config.enabled || !enableLOD || (model && !model->allowLOD)) {
            return LODTier::LOD0_Full;
        }

        // Effective distance thresholds (per-model override or global config)
        const float d01  = (model && model->distanceLOD1) ? *model->distanceLOD1 : m_config.lod0To1Distance;
        const float d12  = (model && model->distanceLOD2) ? *model->distanceLOD2 : m_config.lod1To2Distance;
        const float dMax = (model && model->distanceCull) ? *model->distanceCull : m_config.maxDistance;
        const float hyst = m_config.hysteresis;

        // 1. Tier selection with boundary hysteresis
        LODTier targetTier = currentTier;
        switch (currentTier) {
            case LODTier::LOD0_Full:
                if (distance > dMax + hyst) {
                    targetTier = LODTier::Culled;
                } else if (distance > d12 + hyst) {
                    targetTier = LODTier::LOD2_Impostor;
                } else if (distance > d01 + hyst) {
                    targetTier = LODTier::LOD1_Simplified;
                } else {
                    targetTier = LODTier::LOD0_Full;
                }
                break;

            case LODTier::LOD1_Simplified:
                if (distance > dMax + hyst) {
                    targetTier = LODTier::Culled;
                } else if (distance > d12 + hyst) {
                    targetTier = LODTier::LOD2_Impostor;
                } else if (distance < d01 - hyst) {
                    targetTier = LODTier::LOD0_Full;
                } else {
                    targetTier = LODTier::LOD1_Simplified;
                }
                break;

            case LODTier::LOD2_Impostor:
                if (distance > dMax + hyst) {
                    targetTier = LODTier::Culled;
                } else if (distance < d12 - hyst) {
                    targetTier = (distance < d01 - hyst) ? LODTier::LOD0_Full : LODTier::LOD1_Simplified;
                } else {
                    targetTier = LODTier::LOD2_Impostor;
                }
                break;

            case LODTier::Culled:
                if (distance < d01 - hyst) {
                    targetTier = LODTier::LOD0_Full;
                } else if (distance < d12 - hyst) {
                    targetTier = LODTier::LOD1_Simplified;
                } else if (distance < dMax - hyst) {
                    targetTier = LODTier::LOD2_Impostor;
                } else {
                    targetTier = LODTier::Culled;
                }
                break;
        }

        // 2. Predictable fallback policy when specific tier assets are missing
        if (model) {
            if (targetTier == LODTier::LOD2_Impostor) {
                const bool hasImpostor = model->allowImpostor &&
                    (model->impostorTexture != nullptr || !model->multiViewImpostors.empty());
                if (!hasImpostor) {
                    // Fallback from impostor: try simplified mesh, then full mesh
                    if (model->simplifiedMesh) {
                        targetTier = LODTier::LOD1_Simplified;
                    } else {
                        targetTier = LODTier::LOD0_Full;
                    }
                }
            }
            if (targetTier == LODTier::LOD1_Simplified) {
                if (!model->simplifiedMesh) {
                    // Fallback from simplified: use full mesh
                    targetTier = LODTier::LOD0_Full;
                }
            }
            if (targetTier == LODTier::LOD0_Full) {
                if (!model->fullMesh) {
                    // Fallback if full mesh is missing: try simplified, then impostor
                    if (model->simplifiedMesh) {
                        targetTier = LODTier::LOD1_Simplified;
                    } else if (model->impostorTexture || !model->multiViewImpostors.empty()) {
                        targetTier = LODTier::LOD2_Impostor;
                    }
                }
            }
        }

        return targetTier;
    }

private:
    LODConfig m_config;
};
