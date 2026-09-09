//dino.cpp

#include "dino.h"

#include <chrono>       // for the frame deadline
#include <fstream>      // for the high score file
#include <iostream>
#include <string>
#include <thread>       // for sleep_until
#include <cerrno>       // for errno on a failed read()
#include <csignal>
#include <cstdio>       // for rename and remove
#include <cstdlib>      // for getenv
#include <unistd.h>     // for read
#include <sys/select.h> // for key input detection
#include <sys/ioctl.h>  // for getting the terminal size
#include <sys/stat.h>   // for mkdir
#include <termios.h>    // for terminal settings

// Dinosaur ASCII art (facing right). Each row is padded to DINO_W characters.
const std::string DINO_AA[] = {
    "   ___ ",
    "  /o  |",
    "_/    |",
    " |_||_|"
};

// Cactus (saguaro) ASCII art. Each row is padded to CACTUS_W characters.
const std::string CACTUS_AA[] = {
    "  |  ",
    "| | |",
    "|_|_|",
    "  |  "
};

// --- High score storage ---------------------------------------------------

// The directory the high score lives in, per the XDG base directory spec.
// Empty when neither XDG_DATA_HOME nor HOME is set, in which case the score
// simply is not persisted.
static std::string HighScoreDirectory() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') return std::string(xdg) + "/dino";
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') return std::string(home) + "/.local/share/dino";
    return std::string();
}

// mkdir -p. Every failure is ignored, the usual EEXIST included: the caller
// finds out whether it worked by trying to write the file.
static void MakeDirectories(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i < path.size() && path[i] != '/') continue;
        mkdir(path.substr(0, i).c_str(), 0755);
    }
}

long LoadHighScore() {
    const std::string dir = HighScoreDirectory();
    if (dir.empty()) return 0;

    std::ifstream in((dir + "/highscore").c_str());
    long value = 0;
    // Missing, unreadable, empty and non-numeric all mean the same thing here:
    // there is no high score yet. None of them is worth a message.
    if (!in || !(in >> value) || value < 0) return 0;
    return value;
}

void SaveHighScore(long value) {
    const std::string dir = HighScoreDirectory();
    if (dir.empty()) return;

    MakeDirectories(dir); // Created lazily, on the first score worth keeping
    const std::string path = dir + "/highscore";
    const std::string temp = path + ".tmp";

    {
        std::ofstream out(temp.c_str(), std::ios::trunc);
        if (!out) return;      // Read-only or missing directory: give up quietly
        out << value << "\n";
        out.flush();
        if (!out) { std::remove(temp.c_str()); return; }
    }

    // Write to a temporary file and rename it into place: rename is atomic
    // within a filesystem, so an interrupted write cannot leave a half-written
    // score behind for the next run to read.
    if (std::rename(temp.c_str(), path.c_str()) != 0) std::remove(temp.c_str());
}

// Set by SIGINT (Ctrl+C), SIGTERM and SIGHUP: unwind the loop through Cleanup()
// so the terminal is always restored, whichever of them arrives.
static volatile sig_atomic_t g_interrupted = 0;
// Set by SIGTSTP (Ctrl+Z). Handled at the top of the game loop rather than in
// the handler itself, because restoring the screen is not async-signal-safe.
static volatile sig_atomic_t g_suspend_requested = 0;
// Set by SIGWINCH: the window changed size and the geometry must be re-derived.
static volatile sig_atomic_t g_resized = 0;

// --- Function to make keyboard input non-blocking (immediately detectable) on Linux ---
bool SetTerminalMode(bool raw) {
    static struct termios oldt;
    static bool saved = false; // Guards against restoring settings we never saved
    if (raw) {
        if (tcgetattr(STDIN_FILENO, &oldt) != 0) return false; // Save the current settings
        struct termios newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO); // No Enter required; hide typed characters
        if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0) return false;
        saved = true;
        return true;
    }
    if (!saved) return false;
    return tcsetattr(STDIN_FILENO, TCSANOW, &oldt) == 0; // Restore the original settings
}

// Function equivalent to Windows' _kbhit() (check whether a key has been pressed)
bool IsKeyPressed() {
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

// Get the terminal size. Returns false if it cannot be determined, which in
// practice means stdout is not a terminal.
bool GetTerminalSize(int& width, int& height) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        width = ws.ws_col;
        height = ws.ws_row;
        return true;
    }
    return false;
}

// Refuse to run where the game cannot be seen or cannot be drawn correctly.
// Called before Game is constructed, because the constructor derives ground_y
// and the play field from the terminal size and has no way to report a problem.
bool CheckTerminalEnvironment(std::string& error) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        error = "dino: must be run in a terminal";
        return false;
    }

    int width, height;
    if (!GetTerminalSize(width, height)) {
        error = "dino: could not determine the terminal size";
        return false;
    }

    if (width < MIN_TERM_WIDTH || height < MIN_TERM_HEIGHT) {
        error = "dino: terminal too small (" + std::to_string(width) + "x" +
                std::to_string(height) + "); at least " +
                std::to_string(MIN_TERM_WIDTH) + "x" +
                std::to_string(MIN_TERM_HEIGHT) + " is required";
        return false;
    }

    return true;
}

// Switch to the alternate screen buffer (what vim, less and htop use) and hide
// the cursor. The terminal keeps the user's previous screen and restores it on
// the way out, so nothing they were looking at is destroyed.
static void EnterGameScreen() {
    std::cout << "\033[?1049h\033[H\033[?25l" << std::flush;
}

// Show the cursor again and leave the alternate screen buffer. No erase is
// needed: leaving the buffer is what brings the user's screen back.
static void LeaveGameScreen() {
    std::cout << "\033[?25h\033[?1049l" << std::flush;
}

// SIGINT / SIGTERM / SIGHUP handler: record which signal arrived and break out
// of the loop, so every one of them leaves through Cleanup() with the terminal
// restored. The number is kept so the exit status can follow the 128+N shell
// convention rather than reporting a plain success.
static void HandleTerminate(int sig) {
    g_interrupted = sig;
}

// SIGTSTP handler: only record the request. The actual suspend happens at the
// top of the game loop, where it is safe to write to the terminal.
static void HandleTstp(int) {
    g_suspend_requested = 1;
}

// SIGWINCH handler: the window was resized. The size is re-read from the loop.
static void HandleWinch(int) {
    g_resized = 1;
}

// std::time has one-second resolution, so two runs started inside the same
// second used to produce an identical layout. std::random_device does not.
Game::Game() : Game(std::random_device{}()) {}

Game::Game(unsigned int seed) : rng(seed) {
    // Initial state of the player (dinosaur). Set before the geometry, because
    // ApplyTerminalSize() places the dinosaur on the ground it derives.
    dino_x = 5;
    dino_y = 0;
    dino_y_float = 0;
    y_velocity = 0;
    is_on_ground = true;

    // Get the screen size from the terminal. CheckTerminalEnvironment() has
    // already confirmed this succeeds and that the window is large enough.
    int term_w = MIN_TERM_WIDTH, term_h = MIN_TERM_HEIGHT;
    GetTerminalSize(term_w, term_h);
    ApplyTerminalSize(term_w, term_h);

    // State that outlives a restart
    best_score = LoadHighScore();
    new_best = false;
    quit = false;
    input_closed = false;
    paused_too_small = false;
    terminal_lost = false;

    Reset();
}

// Start a fresh run. Deliberately leaves the terminal geometry and the random
// number generator alone, so restarting keeps its seed and never has to tear
// down and re-enter raw mode.
void Game::Reset() {
    dino_x = 5;
    dino_y = ground_y - DINO_H;
    dino_y_float = dino_y;
    y_velocity = 0;
    is_on_ground = true;

    // Initial obstacle placement. Place only one at the right edge of the screen,
    // and always keep at least the minimum gap before the next one spawns
    // (do not set spawn_gap to 0). The gap is randomized per run and per spawn.
    obstacles.clear();
    obstacles.push_back(screen_width - 1);
    spawn_gap = RandomGap();

    score = 0;
    game_over = false;
    new_best = false;
    input_freeze = 0;
    frame_delay = 40000; // Initial per-frame wait (40 ms)
}

// Derive every geometry-dependent value from a terminal size. Called once at
// construction and again on every SIGWINCH, so the simulation and what is on
// screen can never disagree about how big the play field is.
void Game::ApplyTerminalSize(int term_w, int term_h) {
    screen_width = term_w;
    screen_height = term_h - 1; // Reserve the bottom row for the score display
    ground_y = screen_height - 2;

    // Keep the dinosaur inside a field that may have got narrower
    if (dino_x > screen_width - DINO_W) dino_x = screen_width - DINO_W;
    if (dino_x < 0) dino_x = 0;

    // Put it back on the ground, which has almost certainly moved. Mid-jump the
    // arc is left alone unless the new ground is now above the dinosaur.
    const int rest_y = ground_y - DINO_H;
    if (is_on_ground || dino_y_float > rest_y) {
        dino_y_float = rest_y;
        dino_y = rest_y;
        is_on_ground = true;
        y_velocity = 0;
    }

    // Size the jump to the field. The apex is whatever clears a cactus, capped
    // by the headroom above the dinosaur's resting position so it can never be
    // thrown off the top of a short terminal. At the supported minimum height
    // the headroom is 5 rows, which is enough; the lower clamp only keeps the
    // arithmetic sane if the geometry is ever smaller than that.
    int apex = CACTUS_H + JUMP_CLEARANCE;
    const int headroom = ground_y - DINO_H;
    if (apex > headroom) apex = headroom;
    if (apex < 1) apex = 1;
    jump_velocity = -4.0 * apex / JUMP_AIR_FRAMES;
    gravity = 8.0 * apex / (JUMP_AIR_FRAMES * JUMP_AIR_FRAMES);

    // Drop obstacles that a narrower field can no longer hold
    for (size_t i = obstacles.size(); i-- > 0;) {
        if (obstacles[i] >= screen_width) {
            obstacles.erase(obstacles.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

// Shown instead of the play field while the window is below the minimum size.
// Writes only short, explicitly positioned strings and never indexes the render
// buffer, so it stays safe all the way down to a 1x1 terminal.
void Game::RenderTooSmall() const {
    int width = 0, height = 0;
    if (!GetTerminalSize(width, height)) return;

    std::string first = "terminal too small";
    std::string second = "need " + std::to_string(MIN_TERM_WIDTH) + "x" +
                         std::to_string(MIN_TERM_HEIGHT);
    if (static_cast<int>(first.size()) > width) first = first.substr(0, width);
    if (static_cast<int>(second.size()) > width) second = second.substr(0, width);

    std::string output = "\033[2J\033[H" + first;
    if (height >= 2) output += "\033[2;1H" + second;
    std::cout << output << std::flush;
}

// --- 1. Input handling ---
void Game::HandleInput() {
    // Once stdin is at EOF, select() reports it readable forever. Stop asking.
    if (input_closed) return;

    // Process all buffered input at once (avoids dropping rapid key presses)
    while (IsKeyPressed()) {
        char ch;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n == 0) { // EOF: stdin is closed, there will never be more input
            input_closed = true;
            return;
        }
        if (n < 0) {
            if (errno == EINTR) continue; // Interrupted by a signal; try again
            input_closed = true;
            return;
        }

        if (game_over) {
            // Keys pressed in the moments around the collision are read and
            // thrown away, so a jump buffered just before impact cannot dismiss
            // the panel before the player has looked at it.
            if (input_freeze > 0) continue;
            if (ch == 'r') {
                Reset();
            } else if (ch == 'q') {
                quit = true;
            }
            continue;
        }

        if ((ch == ' ' || ch == 'w') && is_on_ground) {
            // Jump with space / w. The velocity was derived from the play field
            // in ApplyTerminalSize(), so the arc fits whatever terminal this is.
            y_velocity = jump_velocity;
            is_on_ground = false;
        } else if (ch == 'q') {
            quit = true;
        }
    }
}

// Minimum gap (in columns) always kept between obstacles.
// By reserving the distance traveled while airborne plus the widths of the
// dinosaur and cactus, the player can land after clearing one obstacle and
// have room to prepare for the next.
int Game::MinGap() const {
    return DINO_W + CACTUS_W + 16;
}

// The gap to the next obstacle: the minimum plus a random extra amount. Since
// spawn_gap is in frames (roughly equal to columns), the gap never falls below
// MinGap(), which prevents obstacles from being placed too close together.
// uniform_int_distribution rather than rand() % n, which is biased.
int Game::RandomGap() {
    std::uniform_int_distribution<int> extra(0, screen_width / 3);
    return MinGap() + extra(rng);
}

// Spawn a new obstacle at the right edge of the screen
void Game::SpawnObstacle() {
    obstacles.push_back(screen_width - 1);
    spawn_gap = RandomGap();
}

// --- 2. Physics and update ---
void Game::Update() {
    if (!is_on_ground) {
        y_velocity += gravity;
        dino_y_float += y_velocity;
        dino_y = static_cast<int>(dino_y_float);

        // Check whether the dinosaur has landed
        if (dino_y >= ground_y - DINO_H) {
            dino_y = ground_y - DINO_H;
            dino_y_float = dino_y;
            is_on_ground = true;
            y_velocity = 0;
        }
    }

    // Move obstacles to the left. Remove any that have gone off-screen.
    for (size_t i = 0; i < obstacles.size(); ++i) {
        obstacles[i] -= 1;
    }
    if (!obstacles.empty() && obstacles.front() < -CACTUS_W) {
        obstacles.erase(obstacles.begin());
    }

    // Spawn a new obstacle every fixed distance
    if (--spawn_gap <= 0) {
        SpawnObstacle();
    }

    // Add the survival score and speed up over time
    ++score;
    if (score % 200 == 0 && frame_delay > 18000) {
        frame_delay -= 2000; // Gradually get faster
    }
}

// --- 3. Collision detection ---
// Rather than using bounding boxes, a collision is only registered when the
// actually-drawn cells of the dinosaur and the cactus overlap (pixel-level check).
void Game::CheckCollision() {
    int cactus_y = ground_y - CACTUS_H;
    for (size_t i = 0; i < obstacles.size(); ++i) {
        int cx = obstacles[i];

        // First, skip if the rough bounding boxes do not overlap
        if (dino_x >= cx + CACTUS_W || dino_x + DINO_W <= cx) continue;

        // For each dinosaur cell, check whether a cactus cell exists at the same screen coordinate
        for (int dh = 0; dh < DINO_H; ++dh) {
            for (int dw = 0; dw < DINO_W; ++dw) {
                if (DINO_AA[dh][dw] == ' ') continue;

                int sx = dino_x + dw; // x on screen
                int sy = dino_y + dh; // y on screen

                int cw = sx - cx;        // Column within the cactus art
                int chh = sy - cactus_y; // Row within the cactus art
                if (cw < 0 || cw >= CACTUS_W || chh < 0 || chh >= CACTUS_H) continue;

                if (CACTUS_AA[chh][cw] != ' ') {
                    game_over = true;
                    return;
                }
            }
        }
    }
}

// Write `text` into the buffer at (y, x), clipped to the buffer's bounds.
static void Stamp(std::vector<std::string>& screen, int y, int x,
                  const std::string& text) {
    if (y < 0 || y >= static_cast<int>(screen.size())) return;
    for (size_t i = 0; i < text.size(); ++i) {
        const int px = x + static_cast<int>(i);
        if (px < 0 || px >= static_cast<int>(screen[y].size())) continue;
        screen[y][px] = text[i];
    }
}

// Draw the game-over box centred on top of whatever the last frame contained,
// so the player can still see the collision that ended the run.
void Game::DrawGameOverPanel(std::vector<std::string>& screen) const {
    std::vector<std::string> lines;
    lines.push_back("GAME OVER");
    lines.push_back(new_best
                        ? "SCORE " + std::to_string(score) + "    NEW BEST!"
                        : "SCORE " + std::to_string(score) +
                              "    BEST " + std::to_string(best_score));
    lines.push_back("[r] Restart   [q] Quit");

    size_t widest = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].size() > widest) widest = lines[i].size();
    }

    int inner = static_cast<int>(widest) + 4; // padding either side of the text
    if (inner > screen_width - 2) inner = screen_width - 2;
    const int box_w = inner + 2;              // plus the two side borders
    const int box_h = static_cast<int>(lines.size()) + 2;
    if (inner < 1 || box_w > screen_width || box_h > screen_height) return;

    const int x0 = (screen_width - box_w) / 2;
    const int y0 = (screen_height - box_h) / 2;
    const std::string border = "+" + std::string(inner, '-') + "+";
    const std::string blank = "|" + std::string(inner, ' ') + "|";

    Stamp(screen, y0, x0, border);
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        std::string text = lines[i];
        if (static_cast<int>(text.size()) > inner) text = text.substr(0, inner);
        const int pad = (inner - static_cast<int>(text.size())) / 2;
        Stamp(screen, y0 + 1 + i, x0, blank);
        Stamp(screen, y0 + 1 + i, x0 + 1 + pad, text);
    }
    Stamp(screen, y0 + box_h - 1, x0, border);
}

// The status line is the widest fixed element on screen, and the high score
// makes it wider still. Rather than let it overflow a narrow terminal -- which
// would wrap onto a row that does not exist and scroll the whole frame -- drop
// the least important part that does not fit.
std::string Game::BuildStatusLine() const {
    const std::string score_part = " SCORE: " + std::to_string(score);
    const std::string hi_part =
        best_score > 0 ? "   HI: " + std::to_string(best_score) : std::string();
    const std::string hints = game_over ? "   [r] Restart   [q] Quit "
                                        : "   [SPACE/w] Jump   [q] Quit ";

    std::string line = score_part + hi_part + hints;
    if (static_cast<int>(line.size()) > screen_width) line = score_part + hi_part + " ";
    if (static_cast<int>(line.size()) > screen_width) line = score_part + " ";
    if (static_cast<int>(line.size()) > screen_width) {
        line = line.substr(0, static_cast<size_t>(screen_width));
    }
    return line;
}

// --- 4 & 5. Build the draw buffer and flush it to the screen at once ---
void Game::Render() {
    // Reuse the buffer across frames rather than allocating a fresh one every
    // tick; only the contents are reset. resize() keeps the rows that already
    // exist, and assign() reuses each row's capacity.
    if (static_cast<int>(screen_buffer.size()) != screen_height) {
        screen_buffer.resize(screen_height);
    }
    for (int y = 0; y < screen_height; ++y) {
        screen_buffer[y].assign(static_cast<size_t>(screen_width), ' ');
    }
    std::vector<std::string>& screen = screen_buffer;

    // Draw the ground. The bounds check is what keeps a bad ground_y from
    // writing outside the buffer, whatever geometry it was derived from.
    if (ground_y >= 0 && ground_y < screen_height) {
        for (int x = 0; x < screen_width; ++x) {
            screen[ground_y][x] = '_';
        }
    }

    // Write the dinosaur
    for (int h = 0; h < DINO_H; ++h) {
        for (int w = 0; w < DINO_W; ++w) {
            int py = dino_y + h;
            int px = dino_x + w;
            if (py >= 0 && py < screen_height && px >= 0 && px < screen_width) {
                char c = DINO_AA[h][w];
                if (c != ' ') screen[py][px] = c;
            }
        }
    }

    // Write the cacti
    int cactus_y = ground_y - CACTUS_H;
    for (size_t i = 0; i < obstacles.size(); ++i) {
        int cactus_x = obstacles[i];
        for (int h = 0; h < CACTUS_H; ++h) {
            for (int w = 0; w < CACTUS_W; ++w) {
                int px = cactus_x + w;
                int py = cactus_y + h;
                if (px >= 0 && px < screen_width && py >= 0 && py < screen_height) {
                    char c = CACTUS_AA[h][w];
                    if (c != ' ') screen[py][px] = c;
                }
            }
        }
    }

    if (game_over) DrawGameOverPanel(screen);

    // \033[H is the escape sequence that moves the cursor to the top-left (0,0) (prevents flicker)
    std::string& output = output_buffer;
    output.clear(); // Keeps the capacity from the previous frame
    output += "\033[H";
    for (int y = 0; y < screen_height; ++y) {
        output += screen[y];
        if (y < screen_height - 1) output += "\n";
    }
    // Put the status line on the reserved bottom row explicitly and clear it.
    // A restart takes the score back to 0, and without the erase the old, longer
    // number would leave stale digits behind.
    output += "\033[" + std::to_string(screen_height + 1) + ";1H\033[K";
    output += "\033[7m" + BuildStatusLine() + "\033[0m";
    std::cout << output << std::flush;
}

// Restore the terminal state and return to the prompt with the user's screen
// exactly as they left it.
void Game::Cleanup() {
    LeaveGameScreen();
    SetTerminalMode(false);
    // Keys pressed during Update/Render/sleep are still queued and would be
    // echoed at the shell prompt. Drop them.
    tcflush(STDIN_FILENO, TCIFLUSH);
}

// Ctrl+Z. Put the terminal back the way the shell expects it, stop for real by
// re-raising with the default disposition, then restore the game state on the
// way back in. Doing this here rather than in the handler keeps every terminal
// write out of async-signal-unsafe territory.
void Game::SuspendToShell() {
    LeaveGameScreen();
    SetTerminalMode(false);
    tcflush(STDIN_FILENO, TCIFLUSH);

    std::signal(SIGTSTP, SIG_DFL);
    std::raise(SIGTSTP);
    // --- stopped here; execution resumes when the shell sends SIGCONT ---
    std::signal(SIGTSTP, HandleTstp);

    if (!SetTerminalMode(true)) {
        // The terminal is no longer usable; end the run rather than play blind.
        terminal_lost = true;
        return;
    }
    EnterGameScreen();
    g_resized = 1; // The window may have been resized while we were stopped
}

Game::Result Game::Run() {
    // Catch every signal that would otherwise leave the terminal in raw mode
    std::signal(SIGINT, HandleTerminate);   // Ctrl+C
    std::signal(SIGTERM, HandleTerminate);  // kill
    std::signal(SIGHUP, HandleTerminate);   // terminal window closed
    std::signal(SIGTSTP, HandleTstp);       // Ctrl+Z
    std::signal(SIGWINCH, HandleWinch);     // window resized

    // Configure the terminal for the game
    if (!SetTerminalMode(true)) {
        std::cerr << "dino: could not put the terminal into raw mode\n";
        Result failed;
        failed.outcome = Outcome::Failed;
        failed.score = 0;
        failed.signal_number = 0;
        return failed;
    }

    // Move to the alternate screen buffer and hide the cursor
    EnterGameScreen();

    // Game loop. A collision no longer ends it: the run stops on the game-over
    // panel, which offers a restart, and only q or a signal leaves.
    //
    // Frames are paced to a deadline rather than by sleeping a fixed amount
    // after the work: a fixed sleep makes the real frame time delay + work, so
    // the game ran slower the larger the terminal was, and slower again on a
    // busy machine or over a slow link.
    auto next_frame = std::chrono::steady_clock::now();
    while (!quit && !g_interrupted) {
        next_frame += std::chrono::microseconds(frame_delay);
        if (g_suspend_requested) {
            g_suspend_requested = 0;
            SuspendToShell();
            if (terminal_lost) break;
        }

        if (g_resized) {
            g_resized = 0;
            int term_w = 0, term_h = 0;
            if (GetTerminalSize(term_w, term_h)) {
                paused_too_small =
                    (term_w < MIN_TERM_WIDTH || term_h < MIN_TERM_HEIGHT);
                if (!paused_too_small) ApplyTerminalSize(term_w, term_h);
                // Erase once so no cell from the old size is left behind. This
                // is inside the alternate buffer, so the user's screen is safe.
                std::cout << "\033[2J" << std::flush;
            }
        }

        HandleInput();
        if (paused_too_small) {
            // Hold the run rather than draw a field that does not fit
            RenderTooSmall();
        } else if (game_over) {
            if (input_freeze > 0) --input_freeze;
            Render(); // Keep the final frame and its panel on screen
        } else {
            Update();
            CheckCollision();
            if (game_over) {
                if (score > best_score) {
                    best_score = score;
                    new_best = true;
                    SaveHighScore(best_score);
                }
                input_freeze = GAME_OVER_FREEZE_US / frame_delay;
            }
            Render();
        }

        // If the frame overran its budget -- a slow terminal, or the process
        // was stopped with Ctrl+Z and has just resumed -- drop the frames that
        // were missed instead of replaying them all at once.
        const auto now = std::chrono::steady_clock::now();
        if (next_frame < now) {
            next_frame = now;
        } else {
            std::this_thread::sleep_until(next_frame);
        }
    }

    // Cleanup: restore the terminal to its original state, clear the screen, and return to the prompt
    Cleanup();

    // Work out how the run actually ended. The player quitting from the
    // game-over panel still counts as a collision - that is what ended their
    // run - while q pressed mid-run does not.
    Result result;
    result.score = score;
    result.signal_number = 0;
    if (terminal_lost) {
        result.outcome = Outcome::Failed;
    } else if (g_interrupted) {
        result.outcome = Outcome::Interrupted;
        result.signal_number = static_cast<int>(g_interrupted);
    } else if (game_over) {
        result.outcome = Outcome::Collision;
    } else {
        result.outcome = Outcome::Quit;
    }
    return result;
}
