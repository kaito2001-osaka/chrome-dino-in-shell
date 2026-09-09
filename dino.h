//dino.h

#ifndef DINO_GAME_H
#define DINO_GAME_H

#include <random>
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

// Jump shape. The arc is derived from the play field rather than hardcoded, so
// the dinosaur never leaves the top of a short terminal: it rises just far
// enough to clear a cactus (CACTUS_H + JUMP_CLEARANCE rows), capped by the
// headroom available, and the air time is held constant so a jump feels the
// same at every terminal size. For an apex A over JUMP_AIR_FRAMES frames the
// initial velocity is -4A/T and the per-frame gravity is 8A/T^2.
const int JUMP_AIR_FRAMES = 22;
const int JUMP_CLEARANCE = 3;

// How long the game-over panel ignores input, in microseconds. Long enough that
// a jump keypress buffered just before the collision cannot dismiss the panel
// before the player has seen it.
const int GAME_OVER_FREEZE_US = 400000;

// Smallest terminal the game supports. Below this the play field cannot hold
// the ground, the dinosaur and a jumpable cactus, so the game refuses to start.
const int MIN_TERM_WIDTH = 40;
const int MIN_TERM_HEIGHT = 12;

// --- High score persistence ---
// Stored as a single integer under the XDG data directory:
//     ${XDG_DATA_HOME:-$HOME/.local/share}/dino/highscore
// A missing, empty, unreadable or non-numeric file simply means "no high score"
// -- never an error the player has to deal with.
long LoadHighScore();
// Best effort. A directory that cannot be created or written is ignored
// silently: a high score is not worth interrupting a game for.
void SaveHighScore(long value);

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
    // How a run ended. The game already tracked these separately; without them
    // in the return value main() could not tell a deliberate quit from a death.
    enum class Outcome {
        Collision,   // The last run ended on a cactus
        Quit,        // The player pressed q while still running
        Interrupted, // SIGINT, SIGTERM or SIGHUP ended the run
        Failed,      // The terminal could not be configured; nothing was played
    };

    struct Result {
        Outcome outcome;
        long score;        // The full score, not truncated to int
        int signal_number; // Signal that ended the run (Interrupted only), else 0
    };

    Game();                           // Layout seeded unpredictably
    explicit Game(unsigned int seed); // Layout reproducible from a seed

    // Run the game (init, loop, and cleanup). Reports how the run ended
    // alongside the score, so the caller can word the result correctly.
    Result Run();

private:
    void HandleInput();
    void Update();
    void CheckCollision();
    void Render();
    void Reset();       // Start a fresh run without re-entering raw mode
    void SpawnObstacle();
    int MinGap() const; // Minimum gap (in columns) always kept between obstacles
    int RandomGap();    // MinGap() plus a random extra, in columns
    // Re-derive every geometry-dependent value from a terminal size
    void ApplyTerminalSize(int term_w, int term_h);
    void RenderTooSmall() const; // Shown while the window is below the minimum
    std::string BuildStatusLine() const; // Fitted to the terminal width
    // Overlay the game-over box on top of the retained final frame
    void DrawGameOverPanel(std::vector<std::string>& screen) const;
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
    double jump_velocity; // Upward velocity of a jump, derived from the field
    double gravity;       // Downward acceleration per frame, likewise derived
    bool is_on_ground;

    std::mt19937 rng; // Obstacle spacing; seeded per run, or from --seed

    // Render scratch, kept across frames. Rebuilding these every tick cost
    // roughly screen_width * screen_height bytes of allocation per frame, which
    // grew with the terminal area and slowed the game down on big terminals.
    std::vector<std::string> screen_buffer;
    std::string output_buffer;

    // List of obstacle (cactus) x coordinates
    std::vector<int> obstacles;
    int spawn_gap; // Remaining distance until the next obstacle

    long score;
    long best_score;   // Best ever seen, loaded from and saved to disk
    bool new_best;     // Whether the run that just ended beat the stored best
    bool game_over;
    int input_freeze;  // Frames left before the game-over panel accepts input
    bool quit;          // Whether the user aborted with q
    bool input_closed;  // Whether stdin has reached EOF (nothing more to read)
    bool paused_too_small; // Whether the window shrank below the minimum size
    bool terminal_lost;    // Raw mode could not be restored after a Ctrl+Z
    int frame_delay;    // Wait time per frame (microseconds); smaller is faster
};

#endif // DINO_GAME_H
