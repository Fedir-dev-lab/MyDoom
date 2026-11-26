#pragma once

#include <optional>
#include <string>
#include <vector>

#include "world/LevelData.h"
#include "enemies/Enemy.h"

struct EnemySpawn {
    Vector3 position;
    EnemyType type = EnemyType::MantisScout;
};

struct LoadedLevel {
    LevelData data;
    Vector3 spawnPosition;
    float spawnYaw = 0.0f;
    float spawnPitch = 0.0f;
    std::string name;
    std::vector<EnemySpawn> enemySpawns;
};

class LevelLoader {
public:
    // Reads the v1 line-based .level format. On failure returns std::nullopt
    // and writes a human-readable error including the source line.
    static std::optional<LoadedLevel> Load(const std::string& path,
                                           std::string& error);
    static std::optional<LoadedLevel> LoadFromMemory(const std::string& text,
                                                     std::string& error);
};
