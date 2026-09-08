# Dino

A terminal-based Chrome offline dinosaur game, written in C++ for Linux.
Run across the desert and jump over the cacti for as long as you can.

```
   ___
  /o  |                    |          |
_/    |        | | |     | | |      | | |
 |_||_|________|_|_|_____|_|_|______|_|_|___
```

## Features

- Renders in the terminal using ASCII art and ANSI escape sequences
- Uses the full terminal size (auto-detected at startup)
- Pixel-accurate collision detection (only the drawn cells collide)
- Randomized obstacle placement that is different on every run
- Gradually speeds up the longer you survive
- Restores the terminal cleanly on exit (including Ctrl+C) and prints your final score

## Requirements

- A C++ compiler with C++11 support (e.g. `g++`)
- `make`
- A Linux terminal (uses POSIX `termios`, `select`, and `ioctl`)

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

| Key            | Action |
| -------------- | ------ |
| `Space` or `w` | Jump   |
| `q`            | Quit   |

Your score increases the longer you survive. The game ends when you hit a cactus,
and your final score is printed to the command line.

## Project structure

| File         | Description                                              |
| ------------ | -------------------------------------------------------- |
| `dino.h`     | Declarations: constants, terminal helpers, `Game` class  |
| `dino.cpp`   | Implementation of the terminal helpers and game logic    |
| `main.cpp`   | Entry point that creates and runs the game               |
| `Makefile`   | Build configuration                                      |
