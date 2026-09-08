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
    int score = game.Run();
    if (score < 0) return 1; // The game could not start; it already said why

    // After the game ends, print the final score on the normal command line
    std::cout << "Game Over!  Final Score: " << score << std::endl;
    return 0;
}
