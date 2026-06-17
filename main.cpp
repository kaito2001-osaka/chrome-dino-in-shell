//main.cpp

#include "dino.h"

#include <iostream>

int main() {
    Game game;
    int score = game.Run();

    // After the game ends, print the final score on the normal command line
    std::cout << "Game Over!  Final Score: " << score << std::endl;
    return 0;
}
