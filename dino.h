//dino.h

#ifndef DINO_GAME_H
#define DINO_GAME_H

#include <random>
#include <string>
#include <vector>

// Length of an art row, usable in a constant expression so the sprites can be
// checked against their size constants while compiling.
constexpr int ArtRowWidth(const char* row) {
    return *row == '\0' ? 0 : 1 + ArtRowWidth(row + 1);
}

// Dinosaur ASCII art. Defined here, beside the constants the drawing and
// collision loops index it with, so the two cannot drift apart: the array bound
// fixes the row count and the static_asserts fix every row's width. Both are
// compile-time, so editing a row to the wrong length is a build error rather
// than a silent out-of-bounds read.
const int DINO_W = 7;
const int DINO_H = 4;
constexpr const char* DINO_AA[DINO_H] = {
    "   ___ ",
    "  /o  |",
    "_/    |",
    " |_||_|"
};
static_assert(ArtRowWidth(DINO_AA[0]) == DINO_W, "DINO_AA row 0 is not DINO_W wide");
static_assert(ArtRowWidth(DINO_AA[1]) == DINO_W, "DINO_AA row 1 is not DINO_W wide");
static_assert(ArtRowWidth(DINO_AA[2]) == DINO_W, "DINO_AA row 2 is not DINO_W wide");
static_assert(ArtRowWidth(DINO_AA[3]) == DINO_W, "DINO_AA row 3 is not DINO_W wide");

// Crouched dinosaur: longer and, crucially, only two rows tall, so it passes
// under a pterodactyl that a standing dinosaur would walk into.
const int DUCK_W = 8;
const int DUCK_H = 2;
constexpr const char* DUCK_AA[DUCK_H] = {
    "   ____ ",
    "__/o_|_|"
};
static_assert(ArtRowWidth(DUCK_AA[0]) == DUCK_W, "DUCK_AA row 0 is not DUCK_W wide");
static_assert(ArtRowWidth(DUCK_AA[1]) == DUCK_W, "DUCK_AA row 1 is not DUCK_W wide");

// --- Obstacles -------------------------------------------------------------
// Each sits `top_offset` rows above the ground row, so a pterodactyl can fly at
// a height a standing dinosaur cannot pass and a crouched one can.

const int CACTUS_SMALL_W = 3;
const int CACTUS_SMALL_H = 3;
constexpr const char* CACTUS_SMALL_AA[CACTUS_SMALL_H] = {
    " | ",
    "\\|/",
    " | "
};
static_assert(ArtRowWidth(CACTUS_SMALL_AA[0]) == CACTUS_SMALL_W, "CACTUS_SMALL_AA row 0 width");
static_assert(ArtRowWidth(CACTUS_SMALL_AA[1]) == CACTUS_SMALL_W, "CACTUS_SMALL_AA row 1 width");
static_assert(ArtRowWidth(CACTUS_SMALL_AA[2]) == CACTUS_SMALL_W, "CACTUS_SMALL_AA row 2 width");

// The original saguaro, now the large variant.
const int CACTUS_W = 5;
const int CACTUS_H = 4;
constexpr const char* CACTUS_AA[CACTUS_H] = {
    "  |  ",
    "| | |",
    "|_|_|",
    "  |  "
};
static_assert(ArtRowWidth(CACTUS_AA[0]) == CACTUS_W, "CACTUS_AA row 0 is not CACTUS_W wide");
static_assert(ArtRowWidth(CACTUS_AA[1]) == CACTUS_W, "CACTUS_AA row 1 is not CACTUS_W wide");
static_assert(ArtRowWidth(CACTUS_AA[2]) == CACTUS_W, "CACTUS_AA row 2 is not CACTUS_W wide");
static_assert(ArtRowWidth(CACTUS_AA[3]) == CACTUS_W, "CACTUS_AA row 3 is not CACTUS_W wide");

// Two and three small cacti drawn as a single obstacle. Deliberately packed
// with no gap: a wider cluster needs a longer airborne stretch to clear than
// the jump provides.
const int CLUSTER2_W = 6;
const int CLUSTER2_H = 3;
constexpr const char* CLUSTER2_AA[CLUSTER2_H] = {
    " |  | ",
    "\\|/\\|/",
    " |  | "
};
static_assert(ArtRowWidth(CLUSTER2_AA[0]) == CLUSTER2_W, "CLUSTER2_AA row 0 width");
static_assert(ArtRowWidth(CLUSTER2_AA[1]) == CLUSTER2_W, "CLUSTER2_AA row 1 width");
static_assert(ArtRowWidth(CLUSTER2_AA[2]) == CLUSTER2_W, "CLUSTER2_AA row 2 width");

const int CLUSTER3_W = 9;
const int CLUSTER3_H = 3;
constexpr const char* CLUSTER3_AA[CLUSTER3_H] = {
    " |  |  | ",
    "\\|/\\|/\\|/",
    " |  |  | "
};
static_assert(ArtRowWidth(CLUSTER3_AA[0]) == CLUSTER3_W, "CLUSTER3_AA row 0 width");
static_assert(ArtRowWidth(CLUSTER3_AA[1]) == CLUSTER3_W, "CLUSTER3_AA row 1 width");
static_assert(ArtRowWidth(CLUSTER3_AA[2]) == CLUSTER3_W, "CLUSTER3_AA row 2 width");

// Pterodactyl. Flown at PTERO_TOP_OFFSET rows above the ground, which puts it
// through both the standing dinosaur and the arc of an ordinary jump, so it has
// to be ducked rather than jumped.
const int PTERO_W = 5;
const int PTERO_H = 3;
constexpr const char* PTERO_AA[PTERO_H] = {
    "\\___/",
    " \\o/ ",
    "  v  "
};
static_assert(ArtRowWidth(PTERO_AA[0]) == PTERO_W, "PTERO_AA row 0 width");
static_assert(ArtRowWidth(PTERO_AA[1]) == PTERO_W, "PTERO_AA row 1 width");
static_assert(ArtRowWidth(PTERO_AA[2]) == PTERO_W, "PTERO_AA row 2 width");

// Which obstacle, and how it is placed and drawn.
enum class ObstacleKind {
    CactusSmall,
    CactusLarge,
    Cluster2,
    Cluster3,
    Pterodactyl,
};

struct ObstacleArt {
    const char* const* rows;
    int width;
    int height;
    int top_offset; // Rows between the ground row and the sprite's top row
};

// Description of one obstacle kind. Defined in dino.cpp.
const ObstacleArt& ArtFor(ObstacleKind kind);

// The widest obstacle, which is what MinGap() has to leave room for.
const int WIDEST_OBSTACLE_W = CLUSTER3_W;

// The pterodactyl's height above the ground. Chosen so it cuts through both a
// standing dinosaur and the arc of an ordinary jump, while a crouched
// dinosaur - only DUCK_H rows tall - passes underneath.
const int PTERO_TOP_OFFSET = 6;

// A duck lasts this many frames unless a jump cancels it. Terminals report no
// key release, so the crouch is held on a timer rather than while a key is
// down. Long enough to cover an obstacle's whole horizontal overlap window.
const int DUCK_HOLD_FRAMES = 20;

// Scores at which the harder obstacles start appearing, so the opening of a run
// stays teachable.
const long PTERODACTYL_UNLOCK_SCORE = 300;
const long CLUSTER3_UNLOCK_SCORE = 800;

// Points per column shaved off the random slack between obstacles. This is what
// keeps difficulty rising after frame_delay bottoms out at 18 ms around 2200
// points: at 80 columns the slack starts at 26 and reaches zero near 2600, past
// which every obstacle arrives at exactly MinGap() - the floor the issue asks
// the curve to stop at, since anything closer is not clearable.
const long GAP_TIGHTEN_PER_COLUMN = 100;

// Length of a day and of a night, in points.
const long DAY_LENGTH = 700;

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
    ObstacleKind PickObstacleKind(); // Weighted by what the score has unlocked
    bool IsNight() const;            // Whether the palette is currently inverted
    // The dinosaur's current sprite and box, which depend on whether it ducks
    const char* const* DinoArt() const;
    int DinoWidth() const;
    int DinoHeight() const;
    int DinoTop() const;
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
    int duck_frames;      // Frames of crouch left; 0 means standing
    int escape_state;     // Progress through an "ESC [ X" arrow-key sequence

    std::mt19937 rng; // Obstacle spacing; seeded per run, or from --seed

    // Render scratch, kept across frames. Rebuilding these every tick cost
    // roughly screen_width * screen_height bytes of allocation per frame, which
    // grew with the terminal area and slowed the game down on big terminals.
    std::vector<std::string> screen_buffer;
    std::string output_buffer;

    // One obstacle on the field: where it is, and which kind it is
    struct Obstacle {
        int x;
        ObstacleKind kind;
    };
    std::vector<Obstacle> obstacles;
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
