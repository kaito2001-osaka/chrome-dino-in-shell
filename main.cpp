//main.cpp

#include "dino.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void PrintUsage() {
    std::cerr << "usage: dino [--seed <n>]\n"
              << "  --seed <n>  play a reproducible obstacle layout\n";
}

} // namespace

int main(int argc, char* argv[]) {
    bool has_seed = false;
    unsigned long seed = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--seed") && i + 1 < argc) {
            char* end = nullptr;
            const char* text = argv[++i];
            seed = std::strtoul(text, &end, 10);
            if (end == text || *end != '\0') {
                std::cerr << "dino: --seed needs a number, got '" << text << "'\n";
                return 1;
            }
            has_seed = true;
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return 0;
        } else {
            std::cerr << "dino: unrecognised argument '" << arg << "'\n";
            PrintUsage();
            return 1;
        }
    }

    // Refuse to run where the game cannot be seen or drawn correctly, before
    // Game is constructed and derives its play field from the terminal size.
    std::string error;
    if (!CheckTerminalEnvironment(error)) {
        std::cerr << error << std::endl;
        return 1;
    }

    Game game = has_seed ? Game(static_cast<unsigned int>(seed)) : Game();
    const Game::Result result = game.Run();

    // The wording follows how the run actually ended: only a collision is a
    // game over. Walking away with q is not, and neither is a signal.
    switch (result.outcome) {
        case Game::Outcome::Failed:
            return 1; // Run() has already explained itself on stderr

        case Game::Outcome::Collision:
            std::cout << "Game Over!  Final score: " << result.score << std::endl;
            return 0;

        case Game::Outcome::Quit:
            std::cout << "Score: " << result.score << std::endl;
            return 0;

        case Game::Outcome::Interrupted:
            std::cout << "Interrupted.  Score: " << result.score << std::endl;
            // 128 + N is what a shell reports for a signalled process, so a
            // script can tell an interrupted run from a finished one.
            return 128 + result.signal_number;
    }

    return 0;
}
