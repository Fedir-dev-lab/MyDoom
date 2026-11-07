#pragma once

#include <vector>

#include "world/LevelData.h"

// ---------------------------------------------------------------------------
// LevelState — mutable runtime state for all interactive level elements.
//
// LevelData holds the static definitions (geometry, positions, constraints).
// LevelState holds what has actually happened at runtime: which doors are open,
// which keys have been collected, which message triggers have fired.
//
// Ownership: AppState holds one LevelState that is rebuilt whenever a new
// level is loaded and restored (via SaveData) on load-game.
// ---------------------------------------------------------------------------

// Open fraction of a door: 0.0 = fully closed, 1.0 = fully open.
// The door animates between these extremes over time.
enum class DoorPhase { Closed, Opening, Open, Closing };

struct DoorState {
    int       doorId    = 0;
    DoorPhase phase     = DoorPhase::Closed;
    float     openFraction = 0.0f; // [0, 1]
};

struct KeyState {
    int  keyId     = 0;
    bool collected = false;
};

struct MessageTriggerState {
    int  triggerId = 0;
    bool fired     = false;
};

class LevelState {
public:
    // Build initial state from a freshly loaded LevelData: all doors closed,
    // all keys uncollected, all triggers unfired.
    void Reset(const LevelData& levelData);

    // ----- Accessors -----
    const std::vector<DoorState>&           Doors()           const { return m_doors; }
    const std::vector<KeyState>&            Keys()            const { return m_keys; }
    const std::vector<MessageTriggerState>& MessageTriggers() const { return m_messageTriggers; }

    std::vector<DoorState>&           Doors()           { return m_doors; }
    std::vector<KeyState>&            Keys()            { return m_keys; }
    std::vector<MessageTriggerState>& MessageTriggers() { return m_messageTriggers; }

    // Returns true if the player holds the given key colour.
    // KeyColour::None always returns true (door has no lock).
    bool HasKey(KeyColour colour) const;

    // Returns a pointer to the DoorState for doorId, or nullptr if not found.
    DoorState*       FindDoor(int doorId);
    const DoorState* FindDoor(int doorId) const;

    // Returns a pointer to the KeyState for keyId, or nullptr if not found.
    KeyState*       FindKey(int keyId);
    const KeyState* FindKey(int keyId) const;

    // Returns a pointer to the trigger state for triggerId, or nullptr.
    MessageTriggerState*       FindTrigger(int triggerId);
    const MessageTriggerState* FindTrigger(int triggerId) const;

private:
    const LevelData*                  m_levelData = nullptr; // set by Reset()
    std::vector<DoorState>            m_doors;
    std::vector<KeyState>             m_keys;
    std::vector<MessageTriggerState>  m_messageTriggers;
};
