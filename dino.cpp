//dino.cpp

#include "dino.h"

#include <iostream>
#include <string>
#include <cerrno>       // for errno on a failed read()
#include <csignal>
#include <unistd.h>     // for usleep and read
#include <sys/select.h> // for key input detection
#include <sys/ioctl.h>  // for getting the terminal size
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

// SIGINT / SIGTERM / SIGHUP handler: raise a flag to break out of the loop, so
// every one of them leaves through Cleanup() with the terminal restored.
static void HandleTerminate(int) {
    g_interrupted = 1;
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

    // Initial obstacle placement. Place only one at the right edge of the screen,
    // and always keep at least the minimum gap before the next one spawns
    // (do not set spawn_gap to 0). The gap is randomized per run and per spawn.
    obstacles.clear();
    obstacles.push_back(screen_width - 1);
    spawn_gap = RandomGap();

    score = 0;
    game_over = false;
    quit = false;
    input_closed = false;
    paused_too_small = false;
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

// --- 4 & 5. Build the draw buffer and flush it to the screen at once ---
void Game::Render() {
    std::vector<std::string> screen(screen_height, std::string(screen_width, ' '));

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

    // \033[H is the escape sequence that moves the cursor to the top-left (0,0) (prevents flicker)
    std::string output = "\033[H";
    for (int y = 0; y < screen_height; ++y) {
        output += screen[y];
        if (y < screen_height - 1) output += "\n";
    }
    // Show the score and the controls on the bottom row
    output += "\033[7m SCORE: " + std::to_string(score) +
              "   [SPACE/w] Jump   [q] Quit \033[0m";
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
        g_interrupted = 1;
        return;
    }
    EnterGameScreen();
    g_resized = 1; // The window may have been resized while we were stopped
}

int Game::Run() {
    // Catch every signal that would otherwise leave the terminal in raw mode
    std::signal(SIGINT, HandleTerminate);   // Ctrl+C
    std::signal(SIGTERM, HandleTerminate);  // kill
    std::signal(SIGHUP, HandleTerminate);   // terminal window closed
    std::signal(SIGTSTP, HandleTstp);       // Ctrl+Z
    std::signal(SIGWINCH, HandleWinch);     // window resized

    // Configure the terminal for the game
    if (!SetTerminalMode(true)) {
        std::cerr << "dino: could not put the terminal into raw mode\n";
        return -1;
    }

    // Move to the alternate screen buffer and hide the cursor
    EnterGameScreen();

    // Game loop
    while (!game_over && !quit && !g_interrupted) {
        if (g_suspend_requested) {
            g_suspend_requested = 0;
            SuspendToShell();
            if (g_interrupted) break;
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
        } else {
            Update();
            CheckCollision();
            Render();
        }
        usleep(frame_delay);
    }

    // Cleanup: restore the terminal to its original state, clear the screen, and return to the prompt
    Cleanup();

    return static_cast<int>(score);
}
