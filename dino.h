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

// Smallest terminal the game supports. Below this the play field cannot hold
// the ground, the dinosaur and a jumpable cactus, so the game refuses to start.
const int MIN_TERM_WIDTH = 40;
const int MIN_TERM_HEIGHT = 12;

// --- Terminal control ---
// Put keyboard input into non-blocking (immediately detectable) mode on Linux.
// Returns false if the terminal settings could not be read or applied.
bool SetTerminalMode(bool raw);
// Equivalent of Windows' _kbhit() (check whether a key has been pressed)
bool IsKeyPressed();
// Get the terminal size (columns and rows). Returns false when stdout is not a
// terminal, which is the only reason the size cannot be determined.
bool GetTerminalSize(int& width, int& height);
// Check that the game can actually run here: stdin and stdout are terminals and
// the window is at least MIN_TERM_WIDTH x MIN_TERM_HEIGHT. On failure, fills
// `error` with a message for the user and returns false. Must be called before
// constructing Game, which derives its geometry from the terminal size.
bool CheckTerminalEnvironment(std::string& error);

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
    void SuspendToShell(); // Ctrl+Z: hand the terminal back, stop, then resume
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
    bool quit;          // Whether the user aborted with q
    bool input_closed;  // Whether stdin has reached EOF (nothing more to read)
    int frame_delay;    // Wait time per frame (microseconds); smaller is faster
};

#endif // DINO_GAME_H
