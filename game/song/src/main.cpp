#include "song/game.h"
#include "next/foundation/logger.h"

int main(int argc, char* argv[]) {
    NEXT_LOG_INFO("========================================");
    NEXT_LOG_INFO("NEXT Engine - Song Dynasty Demo (CP0)");
    NEXT_LOG_INFO("========================================");

    Song::Game game;

    if (!game.Initialize()) {
        NEXT_LOG_FATAL("Failed to initialize game");
        return 1;
    }

    game.Run();

    game.Shutdown();

    NEXT_LOG_INFO("========================================");
    NEXT_LOG_INFO("Game exited normally");
    NEXT_LOG_INFO("========================================");

    return 0;
}
