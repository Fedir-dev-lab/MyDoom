#pragma once

#include <vector>

#include "core/Vector3.h"

class Map;
class LevelState;

// A* grid-based pathfinder for NPC navigation around walls and closed doors.
//
// The grid is built from Map collision queries (IsPositionBlocked +
// IsDoorPositionBlocked) using a fixed cell size.  A* with 8-directional
// movement finds the shortest grid path, which is then simplified by
// collapsing consecutive waypoints that have direct line-of-sight between
// them.  The result is a short list of waypoints the NPC follows.
class Pathfinder {
public:
    Pathfinder(const Map& map, const LevelState& levelState,
               float entityRadius = 0.35f);

    // Compute a path from start to goal.  Returns simplified waypoints
    // (excluding start, including goal).  Empty vector if no path found.
    std::vector<Vector3> FindPath(Vector3 start, Vector3 goal);

    // Query whether a grid cell is navigable. Public so static helpers
    // (e.g. nearest-walkable search) can access it.
    bool IsWalkable(int gx, int gz) const;

private:
    void BuildGrid();
    float Heuristic(int ax, int az, int bx, int bz) const;
    Vector3 CellCenter(int gx, int gz) const;
    void WorldToCell(const Vector3& world, int& gx, int& gz) const;

    const Map&        m_map;
    const LevelState& m_levelState;
    float             m_entityRadius;

    static constexpr float kCellSize = 0.5f;

    float m_minX = 0.0f, m_minZ = 0.0f;
    int   m_gridW = 0,   m_gridH = 0;
    std::vector<bool> m_walkable;
};
