//main.cpp

#include "dino.h"

#include <iostream>
#include <string>

int main() {
    // Refuse to run where the game cannot be seen or drawn correctly, before
    // Game is constructed and derives its play field from the terminal size.
    std::string error;
    if (!CheckTerminalEnvironment(error)) {
        std::cerr << error << std::endl;
        return 1;
    }

    Game game;
    int score = game.Run();
    if (score < 0) return 1; // The game could not start; it already said why

    // After the game ends, print the final score on the normal command line
    std::cout << "Game Over!  Final Score: " << score << std::endl;
    return 0;
}
