#pragma once

#include <string>

// Minimal file logger for startup and recoverable runtime failures. The game
// configures it with SDL's writable per-user preference directory at startup.
class Logger {
public:
    static void Initialize(const std::string& directory);
    static void Info(const std::string& message);
    static void Error(const std::string& message);
    static void Shutdown();

private:
    static void Write(const char* level, const std::string& message);
};
