#pragma once

#include <climits>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "core/Vector2.h"
#include "core/Vector3.h"
#include "world/LevelData.h"
#include "physics/PhysicsWorld.h"

// ---------------------------------------------------------------------------
// OpenWorldManager — chunk-streamed procedural open world.
//
// The world is divided into square chunks of kChunkSize world units.
// A 5×5 tile window around the player is kept active at all times.
// Each chunk contains:
//   · A flat floor sector (one quad, ceilingHeight = nullopt → open sky)
//   · Procedurally placed building blocks (solid wall boxes)
//
// Every frame Update() recomputes which chunks need to load/unload and
// returns a fresh set of sectors + walls for the Render pass.
// The returned data is NOT stored in a Map — Render draws it directly
// via WallRenderer / PlaneRenderer so the existing Map/collision
// (empty LevelData) is not disturbed.
// ---------------------------------------------------------------------------
class OpenWorldManager {
public:
    static constexpr float kChunkSize  = 50.0f;   // world units per chunk side
    static constexpr int   kViewRadius = 2;        // chunks loaded in each direction
    static constexpr float kBuildingChance = 0.35f;

    struct ChunkCoord {
        int cx, cz;
        bool operator==(const ChunkCoord& o) const {
            return cx == o.cx && cz == o.cz;
        }
    };

    struct ChunkCoordHash {
        size_t operator()(const ChunkCoord& k) const {
            return std::hash<int>{}(k.cx) ^ (std::hash<int>{}(k.cz) * 2654435761u);
        }
    };

    // One building block inside a chunk (axis-aligned box).
    struct Building {
        float x, z;        // world-space origin (corner)
        float w, d;        // width (X) and depth (Z)
        float height;      // wall height
    };

    struct Chunk {
        ChunkCoord            coord;
        std::vector<Building> buildings;
        bool                  loaded = false;
    };

    OpenWorldManager() = default;

    // Call once per frame with the player XZ position.
    // Returns true if the visible geometry changed (chunks loaded/unloaded).
    bool Update(float playerX, float playerZ);

    // Physics update — call every frame with dt and camera state.
    void UpdatePhysics(float dt,
                       const Vector3& eyePos,
                       const Vector3& fwdDir);

    // Direct access to physics world for input handling and rendering.
    phys::PhysicsWorld& Physics() { return m_physics; }

    // Renderable geometry for one chunk.
    struct ChunkRenderData {
        ChunkCoord coord;
        Vector3 boundsMin;
        Vector3 boundsMax;
        Sector floor;
        std::vector<std::pair<Wall, float>> walls;
    };

    // Build renderable geometry for the current frame, partitioned by chunk.
    struct RenderData {
        std::vector<ChunkRenderData> chunks;
    };
    const RenderData& GetRenderData() const { return m_renderData; }

    // Spawn position for the open world (centre of chunk 0,0).
    static Vector3 SpawnPosition() { return { kChunkSize * 0.5f, 1.0f, kChunkSize * 0.5f }; }

private:
    void LoadChunk(ChunkCoord coord);
    void UnloadChunk(ChunkCoord coord);
    void RebuildRenderData();

    static float Hash(int x, int z, int salt = 0);
    static std::vector<Building> GenerateBuildings(ChunkCoord coord);

    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> m_chunks;
    RenderData m_renderData;
    phys::PhysicsWorld m_physics;

    int m_lastCX = INT_MIN;
    int m_lastCZ = INT_MIN;
};
