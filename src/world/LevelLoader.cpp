#include "LevelLoader.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace {

bool ReadFloat(std::istringstream& input, float& value) {
    return static_cast<bool>(input >> value);
}

bool ReadInt(std::istringstream& input, int& value) {
    return static_cast<bool>(input >> value);
}

bool IsEndOfCommand(std::istringstream& input) {
    std::string extra;
    return !(input >> extra);
}

std::string ErrorAt(int line, const std::string& message) {
    return "Line " + std::to_string(line) + ": " + message;
}

std::optional<SpriteType> ParseSpriteType(const std::string& value) {
    if (value == "lamp") return SpriteType::Lamp;
    if (value == "ceiling_lamp") return SpriteType::CeilingLamp;
    if (value == "barrel") return SpriteType::Barrel;
    if (value == "column") return SpriteType::Column;
    if (value == "bookshelf") return SpriteType::Bookshelf;
    return std::nullopt;
}

std::optional<KeyColour> ParseKeyColour(const std::string& value) {
    if (value == "none")   return KeyColour::None;
    if (value == "red")    return KeyColour::Red;
    if (value == "blue")   return KeyColour::Blue;
    if (value == "yellow") return KeyColour::Yellow;
    return std::nullopt;
}

bool IsValidLevelId(const std::string& value) {
    // SaveData stores the current ID in a 32-byte null-terminated field.
    if (value.empty() || value.size() >= 32) {
        return false;
    }

    for (const unsigned char character : value) {
        if (!std::isalnum(character) && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<LoadedLevel> LevelLoader::Load(const std::string& path,
                                              std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "Could not open level: " + path;
        return std::nullopt;
    }

    LoadedLevel level;
    std::unordered_map<int, size_t> sectorIndices;
    std::unordered_map<int, bool> spriteIds;
    std::unordered_map<int, bool> pickupIds;
    std::unordered_map<int, bool> lightIds;
    std::unordered_map<int, bool> enemyIds;
    std::unordered_map<int, bool> exitIds;
    std::unordered_map<int, bool> doorIds;
    std::unordered_map<int, bool> msgTriggerIds;
    std::unordered_map<int, bool> keyIds;
    std::unordered_map<int, bool> audioIds;
    std::unordered_map<int, bool> armorIds;
    std::unordered_map<int, bool> hazardIds;
    std::unordered_map<int, bool> meshIds;
    bool hasVersion = false;
    bool hasName = false;
    bool hasSpawn = false;
    bool hasTerrain = false;

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }

        std::istringstream input(line);
        std::string command;
        if (!(input >> command)) { continue; }

        if (command == "terrain") {
            std::string type;
            if (!(input >> type) || !IsEndOfCommand(input) || hasTerrain || type != "mars") {
                error = ErrorAt(lineNumber, "expected at most one 'terrain mars' declaration");
                return std::nullopt;
            }
            level.data.marsTerrain = true;
            hasTerrain = true;
        } else if (command == "version") {
            int version = 0;
            if (!ReadInt(input, version) || !IsEndOfCommand(input) || version != 1 || hasVersion) {
                error = ErrorAt(lineNumber, "expected one 'version 1' declaration");
                return std::nullopt;
            }
            hasVersion = true;
        } else if (command == "name") {
            if (!(input >> level.name) || !IsEndOfCommand(input) || hasName) {
                error = ErrorAt(lineNumber, "expected a single level name");
                return std::nullopt;
            }
            hasName = true;
        } else if (command == "spawn") {
            if (!ReadFloat(input, level.spawnPosition.x) || !ReadFloat(input, level.spawnPosition.y) ||
                !ReadFloat(input, level.spawnPosition.z) || !ReadFloat(input, level.spawnYaw) ||
                !ReadFloat(input, level.spawnPitch) || !IsEndOfCommand(input) || hasSpawn) {
                error = ErrorAt(lineNumber, "expected: spawn x y z yaw pitch");
                return std::nullopt;
            }
            hasSpawn = true;
        } else if (command == "sector") {
            int id = 0;
            float ceiling = 0.0f;
            std::string ceilingToken;
            Sector sector;
            if (!ReadInt(input, id) || id < 0 || sectorIndices.contains(id) ||
                !ReadFloat(input, sector.floorHeight) || !(input >> ceilingToken) ||
                !ReadInt(input, sector.floorMaterial) || !ReadInt(input, sector.ceilingMaterial) ||
                !ReadFloat(input, sector.ambientLight) || !IsEndOfCommand(input) ||
                sector.ambientLight < 0.0f || sector.ambientLight > 1.0f) {
                error = ErrorAt(lineNumber, "invalid sector declaration");
                return std::nullopt;
            }
            if (ceilingToken == "open") {
                sector.ceilingHeight = std::nullopt;
            } else {
                std::istringstream ceilingInput(ceilingToken);
                if (!ReadFloat(ceilingInput, ceiling) || !IsEndOfCommand(ceilingInput) ||
                    ceiling <= sector.floorHeight) {
                    error = ErrorAt(lineNumber, "ceiling must be 'open' or above the floor");
                    return std::nullopt;
                }
                sector.ceilingHeight = ceiling;
            }
            sectorIndices[id] = level.data.sectors.size();
            level.data.sectors.push_back(std::move(sector));
        } else if (command == "vertex") {
            int sectorId = 0;
            float x = 0.0f;
            float z = 0.0f;
            if (!ReadInt(input, sectorId) || !ReadFloat(input, x) || !ReadFloat(input, z) ||
                !IsEndOfCommand(input) || !sectorIndices.contains(sectorId)) {
                error = ErrorAt(lineNumber, "vertex references an unknown sector");
                return std::nullopt;
            }
            level.data.sectors[sectorIndices[sectorId]].vertices.push_back({x, z});
        } else if (command == "wall") {
            int sectorId = 0;
            Wall wall{{}, {}};
            std::string bottom;
            std::string top;
            std::string movement;
            if (!ReadInt(input, sectorId) || !ReadFloat(input, wall.start.x) ||
                !ReadFloat(input, wall.start.z) || !ReadFloat(input, wall.end.x) ||
                !ReadFloat(input, wall.end.z) || !(input >> bottom >> top >> movement) ||
                !IsEndOfCommand(input) || !sectorIndices.contains(sectorId) ||
                (movement != "block" && movement != "pass") ||
                (wall.start.x == wall.end.x && wall.start.z == wall.end.z)) {
                error = ErrorAt(lineNumber, "invalid wall declaration");
                return std::nullopt;
            }
            const Sector& sector = level.data.sectors[sectorIndices[sectorId]];
            auto parseHeight = [](const std::string& token, std::optional<float>& height) {
                if (token == "default") { return true; }
                std::istringstream heightInput(token);
                float value = 0.0f;
                if (!ReadFloat(heightInput, value) || !IsEndOfCommand(heightInput)) return false;
                height = value;
                return true;
            };
            if (!parseHeight(bottom, wall.bottomHeight) || !parseHeight(top, wall.topHeight) ||
                (top == "default" && !sector.ceilingHeight)) {
                error = ErrorAt(lineNumber, "invalid wall height");
                return std::nullopt;
            }
            wall.blocksMovement = movement == "block";
            level.data.sectors[sectorIndices[sectorId]].walls.push_back(std::move(wall));
        } else if (command == "sprite") {
            int id = 0;
            std::string type;
            WorldSprite sprite;
            if (!ReadInt(input, id) || id < 0 || spriteIds.contains(id) || !(input >> type) ||
                !ReadFloat(input, sprite.position.x) || !ReadFloat(input, sprite.position.y) ||
                !ReadFloat(input, sprite.position.z) || !ReadFloat(input, sprite.width) ||
                !ReadFloat(input, sprite.height) || !IsEndOfCommand(input) ||
                sprite.width <= 0.0f || sprite.height <= 0.0f) {
                error = ErrorAt(lineNumber, "invalid sprite declaration");
                return std::nullopt;
            }
            const std::optional<SpriteType> spriteType = ParseSpriteType(type);
            if (!spriteType) {
                error = ErrorAt(lineNumber, "unknown sprite type: " + type);
                return std::nullopt;
            }
            sprite.spriteType = *spriteType;
            spriteIds[id] = true;
            level.data.sprites.push_back(sprite);
        } else if (command == "pickup") {
            int id = 0;
            int amount = 0;
            std::string type;
            Pickup pickup;
            if (!ReadInt(input, id) || id < 0 || pickupIds.contains(id) || !(input >> type) ||
                !ReadFloat(input, pickup.position.x) || !ReadFloat(input, pickup.position.y) ||
                !ReadFloat(input, pickup.position.z) || !ReadInt(input, amount) ||
                !IsEndOfCommand(input) || amount <= 0 || (type != "health" && type != "ammo")) {
                error = ErrorAt(lineNumber, "invalid pickup declaration");
                return std::nullopt;
            }
            if (type == "health") pickup.healthAmount = amount;
            else pickup.ammoAmount = amount;
            pickupIds[id] = true;
            level.data.pickups.push_back(pickup);
        } else if (command == "light") {
            int id = 0;
            PointLight light;
            if (!ReadInt(input, id) || id < 0 || lightIds.contains(id) ||
                !ReadFloat(input, light.position.x) || !ReadFloat(input, light.position.y) ||
                !ReadFloat(input, light.position.z) || !ReadFloat(input, light.radius) ||
                !ReadFloat(input, light.intensity) || !IsEndOfCommand(input) ||
                light.radius <= 0.0f || light.intensity < 0.0f || light.intensity > 1.0f) {
                error = ErrorAt(lineNumber, "invalid light declaration");
                return std::nullopt;
            }
            light.radiusSq = light.radius * light.radius;
            lightIds[id] = true;
            level.data.pointLights.push_back(light);
        } else if (command == "enemy") {
            int id = 0;
            if (!ReadInt(input, id) || id < 0 || enemyIds.contains(id)) {
                error = ErrorAt(lineNumber, "invalid enemy id");
                return std::nullopt;
            }
            std::string token;
            if (!(input >> token)) {
                error = ErrorAt(lineNumber, "incomplete enemy declaration");
                return std::nullopt;
            }
            EnemySpawn enemy;
            if (token == "mantis_scout" || token == "chaser") {
                // "chaser" is the old prototype spelling. New campaign maps
                // use the scenario-facing mantis_scout name.
                enemy.type = EnemyType::MantisScout;
                if (!ReadFloat(input, enemy.position.x) || !ReadFloat(input, enemy.position.y) ||
                    !ReadFloat(input, enemy.position.z) || !IsEndOfCommand(input)) {
                    error = ErrorAt(lineNumber, "invalid enemy coordinates");
                    return std::nullopt;
                }
            } else if (token == "shooter") {
                enemy.type = EnemyType::Shooter;
                if (!ReadFloat(input, enemy.position.x) || !ReadFloat(input, enemy.position.y) ||
                    !ReadFloat(input, enemy.position.z) || !IsEndOfCommand(input)) {
                    error = ErrorAt(lineNumber, "invalid enemy coordinates");
                    return std::nullopt;
                }
            } else if (token == "tank") {
                enemy.type = EnemyType::Tank;
                if (!ReadFloat(input, enemy.position.x) || !ReadFloat(input, enemy.position.y) ||
                    !ReadFloat(input, enemy.position.z) || !IsEndOfCommand(input)) {
                    error = ErrorAt(lineNumber, "invalid enemy coordinates");
                    return std::nullopt;
                }
            } else {
                // Backward compatibility: token is x coordinate
                std::istringstream xStream(token);
                if (!ReadFloat(xStream, enemy.position.x) || !IsEndOfCommand(xStream) ||
                    !ReadFloat(input, enemy.position.y) || !ReadFloat(input, enemy.position.z) ||
                    !IsEndOfCommand(input)) {
                    error = ErrorAt(lineNumber, "invalid enemy declaration");
                    return std::nullopt;
                }
                enemy.type = EnemyType::MantisScout;
            }
            enemyIds[id] = true;
            level.enemySpawns.push_back(enemy);
        } else if (command == "exit") {
            int id = 0;
            ExitTrigger trigger{};
            if (!ReadInt(input, id) || id < 0 || exitIds.contains(id) ||
                !(input >> trigger.targetLevel) || !IsValidLevelId(trigger.targetLevel) ||
                !ReadFloat(input, trigger.minX) || !ReadFloat(input, trigger.minZ) ||
                !ReadFloat(input, trigger.maxX) || !ReadFloat(input, trigger.maxZ) ||
                !IsEndOfCommand(input) || trigger.minX >= trigger.maxX ||
                trigger.minZ >= trigger.maxZ) {
                error = ErrorAt(lineNumber, "invalid exit declaration");
                return std::nullopt;
            }
            exitIds[id] = true;
            level.data.exitTriggers.push_back(trigger);
        } else if (command == "door") {
            // door <id> <startX> <startZ> <endX> <endZ> <bottom|default> <top|default> [none|red|blue|yellow]
            int id = 0;
            DoorDef door;
            std::string bottomToken;
            std::string topToken;
            std::string colourToken = "none";
            if (!ReadInt(input, id) || id < 0 || doorIds.contains(id) ||
                !ReadFloat(input, door.wallStart.x) || !ReadFloat(input, door.wallStart.z) ||
                !ReadFloat(input, door.wallEnd.x)   || !ReadFloat(input, door.wallEnd.z) ||
                !(input >> bottomToken >> topToken) ||
                (door.wallStart.x == door.wallEnd.x && door.wallStart.z == door.wallEnd.z)) {
                error = ErrorAt(lineNumber, "invalid door declaration");
                return std::nullopt;
            }
            // Optional key colour (defaults to none)
            input >> colourToken; // ignore failure — field is optional
            if (!IsEndOfCommand(input)) {
                error = ErrorAt(lineNumber, "door: unexpected extra fields");
                return std::nullopt;
            }
            // Parse heights (same semantics as wall bottom/top)
            if (bottomToken == "default") {
                door.bottomHeight = 0.0f;
            } else {
                std::istringstream b(bottomToken);
                if (!ReadFloat(b, door.bottomHeight) || !IsEndOfCommand(b)) {
                    error = ErrorAt(lineNumber, "door: invalid bottom height");
                    return std::nullopt;
                }
            }
            if (topToken == "default") {
                door.topHeight = 2.0f;
            } else {
                std::istringstream t(topToken);
                if (!ReadFloat(t, door.topHeight) || !IsEndOfCommand(t)) {
                    error = ErrorAt(lineNumber, "door: invalid top height");
                    return std::nullopt;
                }
            }
            if (door.topHeight <= door.bottomHeight) {
                error = ErrorAt(lineNumber, "door: top height must be above bottom");
                return std::nullopt;
            }
            const auto colour = ParseKeyColour(colourToken);
            if (!colour) {
                error = ErrorAt(lineNumber, "door: unknown key colour: " + colourToken);
                return std::nullopt;
            }
            door.doorId      = id;
            door.requiredKey = *colour;
            doorIds[id]      = true;
            level.data.doors.push_back(door);
        } else if (command == "msgtrigger") {
            // msgtrigger <id> <minX> <minZ> <maxX> <maxZ> <message text...>
            int id = 0;
            MessageTriggerDef trig;
            if (!ReadInt(input, id) || id < 0 || msgTriggerIds.contains(id) ||
                !ReadFloat(input, trig.minX) || !ReadFloat(input, trig.minZ) ||
                !ReadFloat(input, trig.maxX) || !ReadFloat(input, trig.maxZ) ||
                trig.minX >= trig.maxX || trig.minZ >= trig.maxZ) {
                error = ErrorAt(lineNumber, "invalid msgtrigger declaration");
                return std::nullopt;
            }
            // Rest of the line is the message text
            std::string word;
            while (input >> word) {
                if (!trig.message.empty()) { trig.message += ' '; }
                trig.message += word;
            }
            if (trig.message.empty()) {
                error = ErrorAt(lineNumber, "msgtrigger: message cannot be empty");
                return std::nullopt;
            }
            trig.triggerId = id;
            msgTriggerIds[id] = true;
            level.data.messageTriggers.push_back(trig);
        } else if (command == "key") {
            // key <id> <red|blue|yellow> <x> <y> <z>
            int id = 0;
            std::string colourToken;
            KeyPickupDef keyDef;
            if (!ReadInt(input, id) || id < 0 || keyIds.contains(id) ||
                !(input >> colourToken) ||
                !ReadFloat(input, keyDef.position.x) ||
                !ReadFloat(input, keyDef.position.y) ||
                !ReadFloat(input, keyDef.position.z) ||
                !IsEndOfCommand(input)) {
                error = ErrorAt(lineNumber, "invalid key declaration");
                return std::nullopt;
            }
            const auto colour = ParseKeyColour(colourToken);
            if (!colour || *colour == KeyColour::None) {
                error = ErrorAt(lineNumber, "key: invalid colour (must be red, blue, or yellow)");
                return std::nullopt;
            }
            keyDef.keyId  = id;
            keyDef.colour = *colour;
            keyIds[id]    = true;
            level.data.keys.push_back(keyDef);
        } else if (command == "audiolog") {
            // audiolog <id> <x> <y> <z> TITLE | text body
            // Title and text are separated by '|' on the same line.
            int id = 0;
            AudiologDef audio;
            if (!ReadInt(input, id) || id < 0) {
                error = ErrorAt(lineNumber, "audiolog: invalid id");
                return std::nullopt;
            }
            if (audioIds.contains(id)) {
                error = ErrorAt(lineNumber, "audiolog: duplicate id " + std::to_string(id));
                return std::nullopt;
            }
            if (!ReadFloat(input, audio.position.x) ||
                !ReadFloat(input, audio.position.y) ||
                !ReadFloat(input, audio.position.z)) {
                error = ErrorAt(lineNumber, "audiolog: expected x y z coordinates");
                return std::nullopt;
            }
            // Rest of line: "TITLE | body text"
            std::string rest;
            std::getline(input, rest);
            const size_t trimStart = rest.find_first_not_of(" \t");
            if (trimStart == std::string::npos) {
                error = ErrorAt(lineNumber, "audiolog: missing title and text");
                return std::nullopt;
            }
            rest = rest.substr(trimStart);
            const size_t pipe = rest.find('|');
            if (pipe == std::string::npos) {
                error = ErrorAt(lineNumber, "audiolog: missing '|' separator between title and text");
                return std::nullopt;
            }
            audio.title = rest.substr(0, pipe);
            audio.text  = rest.substr(pipe + 1);
            while (!audio.title.empty() &&
                   std::isspace(static_cast<unsigned char>(audio.title.back())))
                audio.title.pop_back();
            const size_t textStart = audio.text.find_first_not_of(" \t");
            if (textStart != std::string::npos)
                audio.text = audio.text.substr(textStart);
            if (audio.title.empty() || audio.text.empty()) {
                error = ErrorAt(lineNumber, "audiolog: title and text must not be empty");
                return std::nullopt;
            }
            audio.audioId = id;
            audioIds[id] = true;
            level.data.audiologs.push_back(std::move(audio));
        } else if (command == "armor") {
            // armor <id> <x> <y> <z> [amount]   (amount defaults to 50)
            int id = 0;
            ArmorPickupDef armorDef;
            if (!ReadInt(input, id) || id < 0) {
                error = ErrorAt(lineNumber, "armor: invalid id");
                return std::nullopt;
            }
            if (armorIds.contains(id)) {
                error = ErrorAt(lineNumber, "armor: duplicate id " + std::to_string(id));
                return std::nullopt;
            }
            if (!ReadFloat(input, armorDef.position.x) ||
                !ReadFloat(input, armorDef.position.y) ||
                !ReadFloat(input, armorDef.position.z)) {
                error = ErrorAt(lineNumber, "armor: expected x y z coordinates");
                return std::nullopt;
            }
            int amount = 0;
            if (input >> amount) {
                if (amount <= 0) {
                    error = ErrorAt(lineNumber, "armor: amount must be positive");
                    return std::nullopt;
                }
                armorDef.armorAmount = amount;
            }
            if (!IsEndOfCommand(input)) {
                error = ErrorAt(lineNumber, "armor: unexpected extra fields");
                return std::nullopt;
            }
            armorDef.armorId = id;
            armorIds[id] = true;
            level.data.armorPickups.push_back(armorDef);
        } else if (command == "hazard") {
            // hazard <id> <acid|coolant> <minX> <minZ> <maxX> <maxZ> [dps]
            int id = 0;
            HazardSurface hazard;
            std::string typeToken;
            if (!ReadInt(input, id) || id < 0) {
                error = ErrorAt(lineNumber, "hazard: invalid id");
                return std::nullopt;
            }
            if (hazardIds.contains(id)) {
                error = ErrorAt(lineNumber, "hazard: duplicate id " + std::to_string(id));
                return std::nullopt;
            }
            if (!(input >> typeToken)) {
                error = ErrorAt(lineNumber, "hazard: expected type (acid|coolant)");
                return std::nullopt;
            }
            if (typeToken == "acid") {
                hazard.type = HazardType::Acid;
            } else if (typeToken == "coolant") {
                hazard.type = HazardType::Coolant;
            } else {
                error = ErrorAt(lineNumber,
                    "hazard: unknown type '" + typeToken + "' (use acid or coolant)");
                return std::nullopt;
            }
            if (!ReadFloat(input, hazard.minX) || !ReadFloat(input, hazard.minZ) ||
                !ReadFloat(input, hazard.maxX) || !ReadFloat(input, hazard.maxZ) ||
                hazard.minX >= hazard.maxX || hazard.minZ >= hazard.maxZ) {
                error = ErrorAt(lineNumber,
                    "hazard: expected minX minZ maxX maxZ (min must be < max)");
                return std::nullopt;
            }
            float dps = 0.0f;
            if (input >> dps) {
                if (dps <= 0.0f) {
                    error = ErrorAt(lineNumber, "hazard: dps must be positive");
                    return std::nullopt;
                }
                hazard.damagePerSecond = dps;
            }
            if (!IsEndOfCommand(input)) {
                error = ErrorAt(lineNumber, "hazard: unexpected extra fields");
                return std::nullopt;
            }
            hazard.hazardId = id;
            hazardIds[id] = true;
            level.data.hazards.push_back(hazard);
        } else if (command == "mesh") {
            // mesh <id> <type> <x> <y> <z> <scale> [yaw]
            //   type must be "cube" (the only procedural mesh so far).
            //   yaw is optional and defaults to 0.
            int id = 0;
            std::string typeToken;
            MeshObjectDef meshObj;
            if (!ReadInt(input, id) || id < 0 || meshIds.contains(id) ||
                !(input >> typeToken) || typeToken != "cube" ||
                !ReadFloat(input, meshObj.position.x) ||
                !ReadFloat(input, meshObj.position.y) ||
                !ReadFloat(input, meshObj.position.z) ||
                !ReadFloat(input, meshObj.scale) ||
                meshObj.scale <= 0.0f) {
                error = ErrorAt(lineNumber,
                    "invalid mesh declaration "
                    "(expected: mesh <id> cube <x> <y> <z> <scale> [yaw])");
                return std::nullopt;
            }
            // Optional yaw rotation about Y (radians).
            float yaw = 0.0f;
            if (input >> yaw) {
                meshObj.yaw = yaw;
            }
            input.clear();  // a failed optional read must not mask trailing junk
            if (!IsEndOfCommand(input)) {
                error = ErrorAt(lineNumber, "mesh: unexpected extra fields");
                return std::nullopt;
            }
            meshObj.meshId = id;
            meshObj.type   = MeshType::Cube;
            meshIds[id]    = true;
            level.data.meshObjects.push_back(meshObj);
        } else {
            error = ErrorAt(lineNumber, "unknown command: " + command);
            return std::nullopt;
        }
    }

    if (!hasVersion || !hasName || !hasSpawn) {
        error = "Level must declare version, name, and spawn";
        return std::nullopt;
    }
    if (level.data.sectors.empty()) {
        error = "Level must contain at least one sector";
        return std::nullopt;
    }
    for (const Sector& sector : level.data.sectors) {
        if (sector.vertices.size() < 3) {
            error = "Every sector must contain at least three vertices";
            return std::nullopt;
        }
    }
    return level;
}
