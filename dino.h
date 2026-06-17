//dino.h

#ifndef DINO_GAME_H
#define DINO_GAME_H

#include <string>
#include <vector>

// Dinosaur ASCII art
extern const std::string DINO_AA[];
const int DINO_W = 7;
const int DINO_H = 4;

// Cactus ASCII art
extern const std::string CACTUS_AA[];
const int CACTUS_W = 5;
const int CACTUS_H = 4;

// --- Terminal control ---
// Put keyboard input into non-blocking (immediately detectable) mode on Linux
void SetTerminalMode(bool raw);
// Equivalent of Windows' _kbhit() (check whether a key has been pressed)
bool IsKeyPressed();
// Get the terminal size (columns and rows). Falls back to defaults if unavailable.
void GetTerminalSize(int& width, int& height);

// --- Game core ---
class Game {
public:
    Game();

    // Run the game (init, loop, and cleanup). Returns the final score.
    int Run();

private:
    void HandleInput();
    void Update();
    void CheckCollision();
    void Render();
    void SpawnObstacle();
    int MinGap() const; // Minimum gap (in columns) always kept between obstacles
    void Cleanup();

    // Screen size (obtained from the terminal at runtime)
    int screen_width;
    int screen_height; // Height used for the game area (bottom row is reserved for the score)
    int ground_y;      // Ground level (row)

    // Player (dinosaur) data
    int dino_x;
    int dino_y;
    double dino_y_float;
    double y_velocity;
    bool is_on_ground;

    // List of obstacle (cactus) x coordinates
    std::vector<int> obstacles;
    int spawn_gap; // Remaining distance until the next obstacle

    long score;
    bool game_over;
    bool quit;        // Whether the user aborted with q
    int frame_delay;  // Wait time per frame (microseconds); smaller is faster
};

#endif // DINO_GAME_H
