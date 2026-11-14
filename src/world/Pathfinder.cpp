#include "Pathfinder.h"

#include <algorithm>
#include <cmath>

#include "world/Map.h"
#include "world/LevelState.h"

// ---------------------------------------------------------------------------
Pathfinder::Pathfinder(const Map& map, const LevelState& levelState,
                       float entityRadius)
    : m_map(map), m_levelState(levelState), m_entityRadius(entityRadius) {
    BuildGrid();
}

// ---------------------------------------------------------------------------
// Build a walkable/blocked grid covering all sector vertices plus a margin.
// Each cell center is tested against Map collision queries so the grid
// reflects both static walls and the current door state.
// ---------------------------------------------------------------------------
void Pathfinder::BuildGrid() {
    float maxX = -1e30f, maxZ = -1e30f;
    m_minX = 1e30f;
    m_minZ = 1e30f;

    for (const Sector& sector : m_map.Sectors()) {
        for (const Vector2& v : sector.vertices) {
            m_minX = std::min(m_minX, v.x);
            m_minZ = std::min(m_minZ, v.y);
            maxX   = std::max(maxX,   v.x);
            maxZ   = std::max(maxZ,   v.y);
        }
    }

    // 2-unit margin so the grid extends slightly beyond walls.
    m_minX -= 2.0f; m_minZ -= 2.0f;
    maxX   += 2.0f; maxZ   += 2.0f;

    m_gridW = static_cast<int>((maxX - m_minX) / kCellSize) + 1;
    m_gridH = static_cast<int>((maxZ - m_minZ) / kCellSize) + 1;

    m_walkable.resize(static_cast<size_t>(m_gridW) * m_gridH);
    for (int gz = 0; gz < m_gridH; ++gz) {
        for (int gx = 0; gx < m_gridW; ++gx) {
            Vector3 pos = CellCenter(gx, gz);
            m_walkable[static_cast<size_t>(gz) * m_gridW + gx] =
                !m_map.IsPositionBlocked(pos, m_entityRadius) &&
                !m_map.IsDoorPositionBlocked(pos, m_entityRadius, m_levelState);
        }
    }
}

// ---------------------------------------------------------------------------
Vector3 Pathfinder::CellCenter(int gx, int gz) const {
    return { m_minX + (gx + 0.5f) * kCellSize, 0.0f,
             m_minZ + (gz + 0.5f) * kCellSize };
}

void Pathfinder::WorldToCell(const Vector3& world, int& gx, int& gz) const {
    gx = static_cast<int>((world.x - m_minX) / kCellSize);
    gz = static_cast<int>((world.z - m_minZ) / kCellSize);
    gx = std::clamp(gx, 0, m_gridW - 1);
    gz = std::clamp(gz, 0, m_gridH - 1);
}

bool Pathfinder::IsWalkable(int gx, int gz) const {
    if (gx < 0 || gx >= m_gridW || gz < 0 || gz >= m_gridH) return false;
    return m_walkable[static_cast<size_t>(gz) * m_gridW + gx];
}

float Pathfinder::Heuristic(int ax, int az, int bx, int bz) const {
    const float dx = static_cast<float>(ax - bx);
    const float dz = static_cast<float>(az - bz);
    return std::sqrt(dx * dx + dz * dz);
}

// ---------------------------------------------------------------------------
// Find nearest walkable cell within a small expanding-ring search.
// Returns true and sets (outX, outZ) if found, false otherwise.
// ---------------------------------------------------------------------------
static bool FindNearestWalkable(const Pathfinder* pf, int cx, int cz,
                                int& outX, int& outZ) {
    // Spiral search up to 10 cells in each direction.
    for (int r = 1; r <= 10; ++r) {
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dz) != r) continue;
                if (pf->IsWalkable(cx + dx, cz + dz)) {
                    outX = cx + dx;
                    outZ = cz + dz;
                    return true;
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// A* pathfinding with 8-directional movement and corner-cut prevention.
// The raw grid path is then simplified by collapsing waypoints that have
// direct line-of-sight between them, producing a short list of turns.
// ---------------------------------------------------------------------------
std::vector<Vector3> Pathfinder::FindPath(Vector3 start, Vector3 goal) {
    int sx, sz, gx, gz;
    WorldToCell(start, sx, sz);
    WorldToCell(goal,  gx, gz);

    // If start or goal is in a blocked cell, snap to nearest walkable.
    if (!IsWalkable(sx, sz)) {
        if (!FindNearestWalkable(this, sx, sz, sx, sz)) return {};
    }
    if (!IsWalkable(gx, gz)) {
        if (!FindNearestWalkable(this, gx, gz, gx, gz)) return {};
    }

    if (sx == gx && sz == gz) {
        return { goal };
    }

    // 8 movement directions: 4 orthogonal + 4 diagonal.
    static constexpr int kDirX[] = { 0,  0, 1, -1, 1, 1, -1, -1 };
    static constexpr int kDirZ[] = { 1, -1, 0,  0, 1, -1,  1, -1 };
    static constexpr float kDirCost[] = { 1.0f, 1.0f, 1.0f, 1.0f,
                                          1.414f, 1.414f, 1.414f, 1.414f };

    const auto Idx = [this](int x, int z) {
        return static_cast<size_t>(z) * m_gridW + x;
    };

    const size_t gridTotal = static_cast<size_t>(m_gridW) * m_gridH;
    std::vector<float>   gCost(gridTotal, -1.0f);
    std::vector<int>     parent(gridTotal, -1);
    std::vector<uint8_t>  closed(gridTotal, 0);

    struct OpenNode { int x, z; float f; };
    std::vector<OpenNode> open;

    const size_t startIdx = Idx(sx, sz);
    gCost[startIdx] = 0.0f;
    open.push_back({ sx, sz, Heuristic(sx, sz, gx, gz) });

    while (!open.empty()) {
        // Find node with lowest f (linear scan — grids are small).
        int bestI = 0;
        for (int i = 1; i < static_cast<int>(open.size()); ++i) {
            if (open[i].f < open[bestI].f) bestI = i;
        }
        const OpenNode cur = open[bestI];
        open[bestI] = open.back();
        open.pop_back();

        const size_t curIdx = Idx(cur.x, cur.z);
        if (closed[curIdx]) continue;
        closed[curIdx] = 1;

        if (cur.x == gx && cur.z == gz) {
            // Reconstruct raw path from parent chain.
            std::vector<Vector3> rawPath;
            size_t node = curIdx;
            while (node != startIdx && node != static_cast<size_t>(-1)) {
                const int nx = static_cast<int>(node % m_gridW);
                const int nz = static_cast<int>(node / m_gridW);
                rawPath.push_back(CellCenter(nx, nz));
                node = static_cast<size_t>(parent[node]);
            }
            std::reverse(rawPath.begin(), rawPath.end());

            if (rawPath.empty()) return { goal };

            // Simplify: collapse waypoints with direct LoS.
            // Walk from anchor forward, trying to connect to the
            // farthest visible waypoint.  This removes grid-cell
            // noise and leaves only meaningful turns.
            std::vector<Vector3> simplified;
            simplified.push_back(rawPath[0]);
            size_t anchor = 0;
            while (anchor < rawPath.size() - 1) {
                size_t farthest = anchor + 1;
                for (size_t i = rawPath.size() - 1; i > anchor; --i) {
                    if (!m_map.IsLineBlocked(rawPath[anchor], rawPath[i]) &&
                        !m_map.IsDoorLineBlocked(rawPath[anchor], rawPath[i],
                                                 m_levelState)) {
                        farthest = i;
                        break;
                    }
                }
                simplified.push_back(rawPath[farthest]);
                anchor = farthest;
            }

            // Replace last waypoint with the exact goal position.
            simplified.back() = goal;
            return simplified;
        }

        // Expand 8 neighbors.
        for (int d = 0; d < 8; ++d) {
            const int nx = cur.x + kDirX[d];
            const int nz = cur.z + kDirZ[d];
            if (nx < 0 || nx >= m_gridW || nz < 0 || nz >= m_gridH) continue;

            const size_t nIdx = Idx(nx, nz);
            if (closed[nIdx] || !IsWalkable(nx, nz)) continue;

            // Prevent diagonal movement through a corner (both adjacent
            // orthogonal cells must be walkable).
            if (d >= 4) {
                if (!IsWalkable(cur.x + kDirX[d], cur.z) ||
                    !IsWalkable(cur.x, cur.z + kDirZ[d])) continue;
            }

            const float newG = gCost[curIdx] + kDirCost[d];
            if (gCost[nIdx] < 0.0f || newG < gCost[nIdx]) {
                gCost[nIdx] = newG;
                parent[nIdx] = static_cast<int>(curIdx);
                open.push_back({ nx, nz, newG + Heuristic(nx, nz, gx, gz) });
            }
        }
    }

    return {};  // no path found
}
