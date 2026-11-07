// MyDoomEngine — LevelState: mutable runtime state for interactive elements.

#include "LevelState.h"

#include <algorithm>

void LevelState::Reset(const LevelData& levelData) {
    m_levelData = &levelData;

    m_doors.clear();
    m_doors.reserve(levelData.doors.size());
    for (const DoorDef& def : levelData.doors) {
        m_doors.push_back({def.doorId, DoorPhase::Closed, 0.0f});
    }

    m_keys.clear();
    m_keys.reserve(levelData.keys.size());
    for (const KeyPickupDef& def : levelData.keys) {
        m_keys.push_back({def.keyId, false});
    }

    m_messageTriggers.clear();
    m_messageTriggers.reserve(levelData.messageTriggers.size());
    for (const MessageTriggerDef& def : levelData.messageTriggers) {
        m_messageTriggers.push_back({def.triggerId, false});
    }
}

bool LevelState::HasKey(KeyColour colour) const {
    if (colour == KeyColour::None) { return true; }
    if (!m_levelData) { return false; }
    // Find every KeyPickupDef with matching colour; return true if any is collected.
    for (const KeyPickupDef& def : m_levelData->keys) {
        if (def.colour != colour) { continue; }
        const KeyState* ks = FindKey(def.keyId);
        if (ks && ks->collected) { return true; }
    }
    return false;
}

DoorState* LevelState::FindDoor(int doorId) {
    for (DoorState& d : m_doors) {
        if (d.doorId == doorId) { return &d; }
    }
    return nullptr;
}

const DoorState* LevelState::FindDoor(int doorId) const {
    for (const DoorState& d : m_doors) {
        if (d.doorId == doorId) { return &d; }
    }
    return nullptr;
}

KeyState* LevelState::FindKey(int keyId) {
    for (KeyState& k : m_keys) {
        if (k.keyId == keyId) { return &k; }
    }
    return nullptr;
}

const KeyState* LevelState::FindKey(int keyId) const {
    for (const KeyState& k : m_keys) {
        if (k.keyId == keyId) { return &k; }
    }
    return nullptr;
}

MessageTriggerState* LevelState::FindTrigger(int triggerId) {
    for (MessageTriggerState& t : m_messageTriggers) {
        if (t.triggerId == triggerId) { return &t; }
    }
    return nullptr;
}

const MessageTriggerState* LevelState::FindTrigger(int triggerId) const {
    for (const MessageTriggerState& t : m_messageTriggers) {
        if (t.triggerId == triggerId) { return &t; }
    }
    return nullptr;
}
