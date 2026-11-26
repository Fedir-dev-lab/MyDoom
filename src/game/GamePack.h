#pragma once

#include <filesystem>
#include <string>

// A "game pack" (.mdpk) is a single file that contains the game data
// (levels/, textures/, ...) the same way a .pk3/.wad does for UZDoom.
// The engine unpacks it into a cache folder and then reads it like a normal
// asset directory, so the rest of the engine does not need to know about packs.
//
// Format (all integers little-endian):
//   "MDPK" | u32 version(=1) | u32 fileCount
//   repeated fileCount times:  u16 pathLength | path (UTF-8, '/' separators)
//                              u64 dataSize   | data bytes
namespace GamePack {

// Packs every regular file under srcDir into outFile.
bool Pack(const std::filesystem::path& srcDir,
          const std::filesystem::path& outFile, std::string& error);

// Unpacks packFile into destDir (destDir is replaced). Rejects unsafe paths.
bool Extract(const std::filesystem::path& packFile,
             const std::filesystem::path& destDir, std::string& error);

// Finds the game data to use and returns its directory in outDir.
//   gameArg  : value of --game / positional argument (folder or .mdpk), or ""
//   basePath : folder of the executable (with trailing slash), or ""
//   cacheRoot: where packs are unpacked
// With no gameArg it prefers <basePath>/game.mdpk. A loose
// <basePath>/assets folder remains a development fallback.
bool ResolveGameDir(const std::string& gameArg, const std::string& basePath,
                    const std::filesystem::path& cacheRoot,
                    std::string& outDir, std::string& error);

}  // namespace GamePack
