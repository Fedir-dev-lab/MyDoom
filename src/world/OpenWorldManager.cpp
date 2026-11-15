#include "OpenWorldManager.h"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Hash — deterministic float in [0,1) from two ints and a salt.
// ---------------------------------------------------------------------------
float OpenWorldManager::Hash(int x, int z, int salt) {
    unsigned h = static_cast<unsigned>(x) * 374761393u
               + static_cast<unsigned>(z) * 668265263u
               + static_cast<unsigned>(salt) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
}

// ---------------------------------------------------------------------------
// GenerateBuildings — procedural box layout for one chunk.
// A 5×5 grid of cells is tested; each cell either spawns a building or not.
// ---------------------------------------------------------------------------
std::vector<OpenWorldManager::Building>
OpenWorldManager::GenerateBuildings(ChunkCoord coord) {
    std::vector<Building> result;

    // Don't place anything in the origin chunk so the player has clear space.
    if (coord.cx == 0 && coord.cz == 0) return result;

    constexpr int   kGrid     = 5;
    const float cellSize = kChunkSize / kGrid;

    for (int gz = 0; gz < kGrid; ++gz) {
        for (int gx = 0; gx < kGrid; ++gx) {
            const int wx = coord.cx * kGrid + gx;
            const int wz = coord.cz * kGrid + gz;

            if (Hash(wx, wz, 0) > kBuildingChance) continue;

            // Randomise building size and position within the cell.
            const float margin  = cellSize * 0.15f;
            const float maxSide = cellSize - margin * 2.0f;
            const float bw      = maxSide * (0.4f + Hash(wx, wz, 1) * 0.5f);
            const float bd      = maxSide * (0.4f + Hash(wx, wz, 2) * 0.5f);
            const float offX    = margin + Hash(wx, wz, 3) * (maxSide - bw);
            const float offZ    = margin + Hash(wx, wz, 4) * (maxSide - bd);
            const float bh      = 2.0f   + Hash(wx, wz, 5) * 4.0f;

            const float worldX = coord.cx * kChunkSize + gx * cellSize + offX;
            const float worldZ = coord.cz * kChunkSize + gz * cellSize + offZ;

            result.push_back({ worldX, worldZ, bw, bd, bh });
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// LoadChunk / UnloadChunk
// ---------------------------------------------------------------------------
void OpenWorldManager::LoadChunk(ChunkCoord coord) {
    Chunk chunk;
    chunk.coord     = coord;
    chunk.buildings = GenerateBuildings(coord);
    chunk.loaded    = true;
    m_chunks[coord] = std::move(chunk);
}

void OpenWorldManager::UnloadChunk(ChunkCoord coord) {
    m_chunks.erase(coord);
}

// ---------------------------------------------------------------------------
// Update — load/unload chunks around the player, rebuild geometry if changed.
// ---------------------------------------------------------------------------
bool OpenWorldManager::Update(float playerX, float playerZ) {
    const int cx = static_cast<int>(std::floor(playerX / kChunkSize));
    const int cz = static_cast<int>(std::floor(playerZ / kChunkSize));

    if (cx == m_lastCX && cz == m_lastCZ) return false;
    m_lastCX = cx;
    m_lastCZ = cz;

    // Determine desired set.
    std::vector<ChunkCoord> desired;
    for (int dz = -kViewRadius; dz <= kViewRadius; ++dz)
        for (int dx = -kViewRadius; dx <= kViewRadius; ++dx)
            desired.push_back({ cx + dx, cz + dz });

    // Unload chunks outside the view radius.
    std::vector<ChunkCoord> toUnload;
    for (auto& [coord, chunk] : m_chunks) {
        if (std::abs(coord.cx - cx) > kViewRadius ||
            std::abs(coord.cz - cz) > kViewRadius) {
            toUnload.push_back(coord);
        }
    }
    for (const ChunkCoord& c : toUnload) UnloadChunk(c);

    // Load missing chunks.
    for (const ChunkCoord& c : desired) {
        if (m_chunks.find(c) == m_chunks.end()) LoadChunk(c);
    }

    RebuildRenderData();
    return true;
}

// ---------------------------------------------------------------------------
// RebuildRenderData — convert loaded chunks into renderable sectors + walls.
// ---------------------------------------------------------------------------
void OpenWorldManager::RebuildRenderData() {
    m_renderData.sectors.clear();
    m_renderData.walls.clear();

    for (auto& [coord, chunk] : m_chunks) {
        const float x0 = coord.cx * kChunkSize;
        const float z0 = coord.cz * kChunkSize;
        const float x1 = x0 + kChunkSize;
        const float z1 = z0 + kChunkSize;

        // --- Floor sector (open sky, no ceiling) ---
        Sector floor;
        floor.floorHeight    = 0.0f;
        floor.ceilingHeight  = std::nullopt;  // open sky
        floor.floorMaterial  = 2;             // OutdoorFloor texture
        floor.ambientLight   = 0.4f;
        floor.vertices = {
            { x0, z0 },
            { x1, z0 },
            { x1, z1 },
            { x0, z1 },
        };
        m_renderData.sectors.push_back(std::move(floor));

        // --- Building walls ---
        const float amb = 0.4f;
        for (const Building& b : chunk.buildings) {
            const float bx0 = b.x,        bz0 = b.z;
            const float bx1 = b.x + b.w,  bz1 = b.z + b.d;
            const float top = b.height;

            // South wall (z0 edge)
            m_renderData.walls.push_back({
                Wall({ bx0, 0.f, bz0 }, { bx1, 0.f, bz0 }, 0.f, top, true), amb });
            // North wall (z1 edge)
            m_renderData.walls.push_back({
                Wall({ bx1, 0.f, bz1 }, { bx0, 0.f, bz1 }, 0.f, top, true), amb });
            // West wall (x0 edge)
            m_renderData.walls.push_back({
                Wall({ bx0, 0.f, bz1 }, { bx0, 0.f, bz0 }, 0.f, top, true), amb });
            // East wall (x1 edge)
            m_renderData.walls.push_back({
                Wall({ bx1, 0.f, bz0 }, { bx1, 0.f, bz1 }, 0.f, top, true), amb });
        }
    }
}
