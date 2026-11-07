// mdpack: build or unpack a MyDoomEngine game pack (.mdpk).
//   mdpack <game-folder> <out.mdpk>     pack
//   mdpack -x <in.mdpk> <out-folder>    unpack (for checking a pack)
#include <cstdio>
#include <cstring>
#include <string>

#include "game/GamePack.h"

int main(int argc, char** argv) {
    std::string error;
    if (argc == 4 && std::strcmp(argv[1], "-x") == 0) {
        if (!GamePack::Extract(argv[2], argv[3], error)) {
            std::fprintf(stderr, "mdpack: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
    if (argc == 3) {
        if (!GamePack::Pack(argv[1], argv[2], error)) {
            std::fprintf(stderr, "mdpack: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
    std::fprintf(stderr,
                 "usage:\n  mdpack <game-folder> <out.mdpk>\n"
                 "  mdpack -x <in.mdpk> <out-folder>\n");
    return 2;
}
