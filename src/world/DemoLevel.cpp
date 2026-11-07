#include "DemoLevel.h"

LevelData CreateDemoLevel() {
    return {
        {
            {0.0f, 2.0f, 0, 0, 0.15f,
             {{-6, -6}, {6, -6}, {6, 6}, {-6, 6}},
             {{{-6, 0, 6}, {-1, 0, 6}}, {{1, 0, 6}, {6, 0, 6}},
              {{-1, 0, 6}, {1, 0, 6}, 2.0f, 3.0f, false},
              {{6, 0, 6}, {6, 0, -6}}, {{6, 0, -6}, {-6, 0, -6}},
              {{-6, 0, -6}, {-6, 0, 6}}}},
            {0.0f, 3.0f, 1, 1, 0.15f,
             {{-6, 6}, {6, 6}, {6, 18}, {-6, 18}},
             {{{-6, 0, 6}, {-1, 0, 6}}, {{1, 0, 6}, {6, 0, 6}},
              {{6, 0, 6}, {6, 0, 18}}, {{-6, 0, 18}, {-6, 0, 6}}}},
            {0.0f, std::nullopt, 2, 0, 0.4f,
             {{-14, 18}, {14, 18}, {14, 50}, {-14, 50}},
             {{{-14, 0, 18}, {-2, 0, 18}}, {{2, 0, 18}, {14, 0, 18}},
              {{14, 0, 18}, {14, 0, 50}}, {{14, 0, 50}, {-14, 0, 50}},
              {{-14, 0, 50}, {-14, 0, 18}}}},
        },
        {
            {{-5, 0, 27}, 1.0f, 3.0f, SpriteType::Lamp},
            {{6, 0, 36}, 1.0f, 3.0f, SpriteType::Lamp},
            {{-8, 0, 44}, 1.0f, 3.0f, SpriteType::Lamp},
            {{0, 0, 0}, 0.7f, 2.0f, SpriteType::CeilingLamp},
            {{-5, 0, -4.5f}, 0.8f, 2.0f, SpriteType::Column},
            {{5, 0, -2}, 1.2f, 2.0f, SpriteType::Bookshelf},
            {{0, 0, 12}, 0.7f, 3.0f, SpriteType::CeilingLamp},
            {{5, 0, 8}, 0.9f, 1.2f, SpriteType::Barrel},
            {{-5, 0, 16}, 0.9f, 1.2f, SpriteType::Barrel},
            {{10, 0, 22}, 0.9f, 1.2f, SpriteType::Barrel},
            {{-10, 0, 30}, 0.9f, 1.2f, SpriteType::Barrel},
        },
        {{{3, 0, 29}, 3}, {{-3, 0, 33}, 0, 12}},
        {{{-5, 3, 27}, 10.0f, 1.0f}, {{6, 3, 36}, 10.0f, 1.0f},
         {{-8, 3, 44}, 10.0f, 1.0f}, {{0, 1.6f, 0}, 7.0f, 1.0f},
         {{0, 2.4f, 12}, 8.0f, 1.0f}},
        {{"finale", -3.0f, 22.0f, 3.0f, 27.0f}},
        // doors
        {
            {0, {-1.0f, 0.0f, 6.0f}, {1.0f, 0.0f, 6.0f}, 0.0f, 2.0f, KeyColour::None},
            {1, {-2.0f, 0.0f, 18.0f}, {2.0f, 0.0f, 18.0f}, 0.0f, 3.0f, KeyColour::Red},
        },
        // keys
        {
            {0, KeyColour::Red, {4.0f, 0.0f, 14.0f}},
        },
        // messageTriggers
        {
            {0, -2.0f, -2.0f, 2.0f, 2.0f, "FIND THE RED KEY TO UNLOCK THE COURTYARD"},
            {1, -2.0f, 16.0f, 2.0f, 17.5f, "COURTYARD DOOR REQUIRES RED KEY"},
        },
        false, // marsTerrain
        {},    // audiologs
        {},    // armorPickups
        {},    // hazards
        {},    // meshObjects
    };
}
