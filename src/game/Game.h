#pragma once

// Game owns the application lifecycle. Its implementation contains the
// current Input → Update → Render loop and all runtime state; main.cpp only
// constructs this coordinator and returns its exit code.
class Game {
public:
    int Run(int argc = 0, char* argv[] = nullptr);
};
