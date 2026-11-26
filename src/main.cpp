#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "game/Game.h"

int main(int argc, char* argv[]) {
    Game game;
    return game.Run(argc, argv);
}
