# Dino

A terminal-based Chrome offline dinosaur game, written in C++ for Linux, macOS
and other POSIX systems. Run across the desert, jump the cacti and duck the
pterodactyls for as long as you can.

```
                                                    \___/
                                                     \o/
   ___                                    |           v
  /o  |           |        |  |         | | |
_/    |          \|/      \|/\|/        |_|_|
 |_||_|           |        |  |           |
______________________________________________________________
```

## Features

- Renders in the terminal using ASCII art and ANSI escape sequences
- Uses the full terminal size, and follows it when you resize the window
- Pixel-accurate collision detection (only the drawn cells collide)
- Five obstacle types: small and large cacti, clusters of two and three, and a
  pterodactyl that has to be ducked rather than jumped
- Randomized obstacle placement that is different on every run, or reproducible
  on demand with `--seed`
- Gets harder the longer you survive: the game speeds up, and obstacles start
  arriving closer together once the speed has topped out
- A day/night cycle that inverts the field as a run gets long
- Keeps your best score between sessions
- Runs on the alternate screen buffer, so whatever was in your terminal is still
  there when you quit
- Restores the terminal on every exit path: `q`, Ctrl+C, Ctrl+Z, `kill`, or
  closing the window

## Requirements

- A C++ compiler with C++11 support (e.g. `g++`)
- `make`
- A POSIX terminal (uses `termios`, `select` and `ioctl`), so Linux, macOS and
  the BSDs all work
- A terminal at least **40 columns by 12 rows**. The game refuses to start below
  that, because a shorter play field has no room for a jump that clears a cactus

## Build

```sh
make
```

This produces an executable named `dino`.

To remove the build artifacts:

```sh
make clean
```

## Play

```sh
./dino
```

To replay a specific obstacle layout, pass a seed. Runs with the same seed are
identical, which is useful when reporting a bug or comparing two attempts:

```sh
./dino --seed 42
```

### Controls

| Key                        | Action                          |
| -------------------------- | ------------------------------- |
| `Space`, `w` or `Up`       | Jump                            |
| `s` or `Down`              | Duck                            |
| `q`                        | Quit                            |
| `r`                        | Restart, on the game-over screen |

The controls are also shown on the status line at the bottom of the screen while
you play, so you never have to come back here for them. On a narrow terminal the
hints are dropped to make room for the score.

Jumping clears a cactus. It will **not** clear a pterodactyl, which flies at
exactly the height an ordinary jump passes through — duck under it instead.

Your score increases the longer you survive. Hitting an obstacle ends the run and
shows a game-over screen over the final frame, with your score, your best, and
`r` to start again without relaunching.

### High score

Your best score is kept between sessions in

```
${XDG_DATA_HOME:-$HOME/.local/share}/dino/highscore
```

Delete that file to reset it. If the directory cannot be created or written, the
game simply plays without keeping a high score.

### Exit status

| Status | Meaning                                                |
| ------ | ------------------------------------------------------ |
| `0`    | You quit, or the run ended on a collision              |
| `1`    | Could not start: not a terminal, or the window is too small |
| `128+N`| Ended by signal `N`, e.g. `130` for Ctrl+C              |

## Project structure

| File         | Description                                              |
| ------------ | -------------------------------------------------------- |
| `dino.h`     | Declarations: sprites and their size constants, terminal helpers, `Game` class |
| `dino.cpp`   | Implementation of the terminal helpers and game logic    |
| `main.cpp`   | Entry point: argument parsing, environment checks, result reporting |
| `Makefile`   | Build configuration                                      |
