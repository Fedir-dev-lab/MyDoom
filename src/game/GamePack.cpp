#include "game/GamePack.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace GamePack {
namespace {

constexpr char kMagic[4] = {'M', 'D', 'P', 'K'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxFiles = 100000;

void WriteU16(std::ostream& o, std::uint16_t v) {
    const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
    o.write(b, 2);
}
void WriteU32(std::ostream& o, std::uint32_t v) {
    char b[4];
    for (int i = 0; i < 4; ++i) { b[i] = static_cast<char>((v >> (8 * i)) & 0xFF); }
    o.write(b, 4);
}
void WriteU64(std::ostream& o, std::uint64_t v) {
    char b[8];
    for (int i = 0; i < 8; ++i) { b[i] = static_cast<char>((v >> (8 * i)) & 0xFF); }
    o.write(b, 8);
}

bool ReadBytes(std::istream& in, char* out, std::size_t n) {
    in.read(out, static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(in.gcount()) == n;
}
bool ReadU16(std::istream& in, std::uint16_t& v) {
    unsigned char b[2];
    if (!ReadBytes(in, reinterpret_cast<char*>(b), 2)) { return false; }
    v = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    return true;
}
bool ReadU32(std::istream& in, std::uint32_t& v) {
    unsigned char b[4];
    if (!ReadBytes(in, reinterpret_cast<char*>(b), 4)) { return false; }
    v = 0;
    for (int i = 0; i < 4; ++i) { v |= static_cast<std::uint32_t>(b[i]) << (8 * i); }
    return true;
}
bool ReadU64(std::istream& in, std::uint64_t& v) {
    unsigned char b[8];
    if (!ReadBytes(in, reinterpret_cast<char*>(b), 8)) { return false; }
    v = 0;
    for (int i = 0; i < 8; ++i) { v |= static_cast<std::uint64_t>(b[i]) << (8 * i); }
    return true;
}

// A pack entry path must be relative, use '/' only, and never escape the
// destination folder ("..", absolute paths, drive letters, backslashes).
bool IsSafeEntryPath(const std::string& p) {
    if (p.empty() || p.front() == '/') { return false; }
    if (p.find('\\') != std::string::npos || p.find(':') != std::string::npos ||
        p.find('\0') != std::string::npos) { return false; }
    std::size_t start = 0;
    while (start <= p.size()) {
        std::size_t end = p.find('/', start);
        if (end == std::string::npos) { end = p.size(); }
        const std::string part = p.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") { return false; }
        start = end + 1;
    }
    return true;
}

bool ExtractToCache(const fs::path& packFile, const fs::path& cacheRoot,
                    std::string& outDir, std::string& error) {
    const fs::path dest = cacheRoot / packFile.stem();
    if (!Extract(packFile, dest, error)) { return false; }
    outDir = dest.string();
    return true;
}

}  // namespace

bool Pack(const fs::path& srcDir, const fs::path& outFile, std::string& error) {
    try {
        if (!fs::is_directory(srcDir)) {
            error = "not a directory: " + srcDir.string();
            return false;
        }
        std::vector<fs::path> files;
        for (const auto& e : fs::recursive_directory_iterator(srcDir)) {
            if (e.is_regular_file()) { files.push_back(e.path()); }
        }
        std::sort(files.begin(), files.end());  // deterministic output

        std::ofstream out(outFile, std::ios::binary | std::ios::trunc);
        if (!out) { error = "cannot write " + outFile.string(); return false; }

        out.write(kMagic, 4);
        WriteU32(out, kVersion);
        WriteU32(out, static_cast<std::uint32_t>(files.size()));

        for (const fs::path& f : files) {
            const std::string rel = fs::relative(f, srcDir).generic_string();
            if (!IsSafeEntryPath(rel) || rel.size() > 65535) {
                error = "unsupported file name: " + rel;
                return false;
            }
            std::ifstream in(f, std::ios::binary);
            if (!in) { error = "cannot read " + f.string(); return false; }
            const std::uint64_t size = fs::file_size(f);

            WriteU16(out, static_cast<std::uint16_t>(rel.size()));
            out.write(rel.data(), static_cast<std::streamsize>(rel.size()));
            WriteU64(out, size);
            out << in.rdbuf();
        }
        out.flush();
        if (!out) { error = "write failed: " + outFile.string(); return false; }
        return true;
    } catch (const fs::filesystem_error& e) {
        error = e.what();
        return false;
    }
}

bool Extract(const fs::path& packFile, const fs::path& destDir, std::string& error) {
    try {
        std::ifstream in(packFile, std::ios::binary);
        if (!in) { error = "cannot open " + packFile.string(); return false; }
        std::uint64_t remaining = fs::file_size(packFile);

        char magic[4];
        std::uint32_t version = 0, count = 0;
        if (!ReadBytes(in, magic, 4) || !std::equal(magic, magic + 4, kMagic) ||
            !ReadU32(in, version) || !ReadU32(in, count)) {
            error = "not a game pack (.mdpk): " + packFile.string();
            return false;
        }
        if (version != kVersion) { error = "unsupported pack version"; return false; }
        if (count > kMaxFiles) { error = "pack has too many files"; return false; }
        remaining -= 12;

        const fs::path tmp = fs::path(destDir.string() + ".tmp");
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint16_t len = 0;
            std::uint64_t size = 0;
            if (!ReadU16(in, len) || len > remaining - 2) {
                error = "pack is truncated or corrupt";
                fs::remove_all(tmp);
                return false;
            }
            std::string rel(len, '\0');
            if (!ReadBytes(in, rel.data(), len) || !ReadU64(in, size)) {
                error = "pack is truncated or corrupt";
                fs::remove_all(tmp);
                return false;
            }
            remaining -= 2u + len + 8u;
            if (!IsSafeEntryPath(rel)) {
                error = "unsafe path in pack: " + rel;
                fs::remove_all(tmp);
                return false;
            }
            if (size > remaining) {
                error = "pack is truncated or corrupt";
                fs::remove_all(tmp);
                return false;
            }

            const fs::path target = tmp / rel;
            fs::create_directories(target.parent_path());  // never empty: inside tmp
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) { error = "cannot write " + target.string(); fs::remove_all(tmp); return false; }

            std::vector<char> buf(64 * 1024);
            std::uint64_t left = size;
            while (left > 0) {
                const std::size_t chunk =
                    static_cast<std::size_t>(std::min<std::uint64_t>(left, buf.size()));
                if (!ReadBytes(in, buf.data(), chunk)) {
                    error = "pack is truncated or corrupt";
                    fs::remove_all(tmp);
                    return false;
                }
                out.write(buf.data(), static_cast<std::streamsize>(chunk));
                left -= chunk;
            }
            remaining -= size;
        }

        fs::remove_all(destDir);
        if (!destDir.parent_path().empty()) {
            fs::create_directories(destDir.parent_path());
        }
        fs::rename(tmp, destDir);
        return true;
    } catch (const fs::filesystem_error& e) {
        error = e.what();
        return false;
    }
}

bool ResolveGameDir(const std::string& gameArgIn, const std::string& basePath,
                    const fs::path& cacheRoot, std::string& outDir,
                    std::string& error) {
    std::string gameArg = gameArgIn;
    while (gameArg.size() > 1 && (gameArg.back() == '/' || gameArg.back() == '\\')) {
        gameArg.pop_back();
    }

    std::error_code ec;
    if (!gameArg.empty()) {
        if (fs::is_directory(gameArg, ec)) { outDir = gameArg; return true; }
        if (fs::is_regular_file(gameArg, ec)) {
            return ExtractToCache(fs::path(gameArg), cacheRoot, outDir, error);
        }
        error = "game folder or pack not found: " + gameArg;
        return false;
    }

    const std::string loose = basePath + "assets";
    if (fs::is_directory(loose, ec)) { outDir = loose; return true; }

    const std::string pack = basePath + "game.mdpk";
    if (fs::is_regular_file(pack, ec)) {
        return ExtractToCache(fs::path(pack), cacheRoot, outDir, error);
    }

    error = "no game data: expected assets/ or game.mdpk next to the executable, "
            "or run with --game <folder|file.mdpk>";
    return false;
}

}  // namespace GamePack
