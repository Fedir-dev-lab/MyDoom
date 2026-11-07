#include "Logger.h"

#include <fstream>

namespace {

std::ofstream g_logFile;

} // namespace

void Logger::Initialize(const std::string& directory) {
    if (directory.empty()) {
        return;
    }

    std::string path = directory;
    const char lastCharacter = path.back();
    if (lastCharacter != '/' && lastCharacter != '\\') {
        path += '/';
    }
    path += "mydoomengine.log";

    g_logFile.open(path, std::ios::out | std::ios::trunc);
    if (g_logFile) {
        Write("INFO", "MyDoomEngine log started.");
    }
}

void Logger::Info(const std::string& message) {
    Write("INFO", message);
}

void Logger::Error(const std::string& message) {
    Write("ERROR", message);
}

void Logger::Shutdown() {
    if (!g_logFile) {
        return;
    }

    Write("INFO", "MyDoomEngine log closed.");
    g_logFile.close();
}

void Logger::Write(const char* level, const std::string& message) {
    if (!g_logFile) {
        return;
    }

    g_logFile << '[' << level << "] " << message << '\n';
    g_logFile.flush();
}
