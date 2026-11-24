# POSIX Shell (`posix-shell`)

> **A lightweight, modular, zero-dependency Unix command-line interpreter built in C++17 directly on POSIX system calls.**

`C++17` · `POSIX` · `Linux / macOS` · `Zero External Dependencies` · `MIT License`

## Overview

**POSIX Shell** is an interactive Unix shell engineered from scratch in C++17 without relying on `system()`, `popen()`, `readline`, `ncurses`, or the C++ Standard Template Library (STL). Every core shell capability—process creation, multi-stage pipelines, file descriptor redirection, signal dispatching, raw-mode line editing, TAB autocompletion, and persistent history—is implemented directly over low-level POSIX system calls.

The codebase is partitioned into dedicated `include/` and `src/` modules so that parsing, execution, built-in commands, terminal I/O, and job control remain independent, readable, and easy to extend.

## Key Features

| Category | Capabilities |
| :--- | :--- |
| **Interactive Prompt** | Dynamic `<username@hostname:cwd>` prompt with automatic `~` home-directory contraction |
| **Built-in Commands** | Native implementations of `cd` (`.`, `..`, `~`, `-`), `pwd`, `echo`, `ls` (`-a`, `-l`, `-la`, `-al`, multi-path), `pinfo`, `search`, `history`, and `exit` |
| **Process Management** | Foreground and background (`&`) execution via `fork()`, `execvp()`, `setpgid()`, and `waitpid()`, with automatic background reaping via `SIGCHLD` |
| **Multi-Stage Pipelines** | Arbitrary-length command chaining (`cmd1 \| cmd2 \| cmd3`) using `pipe()` and `dup2()` |
| **I/O Redirection** | Input (`<`), output truncate (`>`), output append (`>>`), and combined redirection—fully composable with pipelines and built-ins |
| **Command Chaining** | Semicolon-separated (`;`) command lists and inline background operators (`cmd1 & cmd2`) |
| **Signal Handling** | Interactive `Ctrl+C` (`SIGINT`), `Ctrl+Z` (`SIGTSTP`), and `Ctrl+D` (`EOF`) handling that controls foreground jobs without terminating the shell |
| **TAB Autocomplete** | Custom raw-terminal completion for built-ins, `PATH` executables, relative `./` paths, and files/directories with longest-common-prefix expansion |
| **Persistent History** | Cross-session command history (stores up to 20 commands, displays 10 by default or `history <n>`) with live `Up`/`Down` arrow navigation and inline editing |

## Architecture & Project Structure

```text
posix-shell/
├── include/                  # Public module headers
│   ├── shell.h               # Core Shell class, runtime state, and constants
│   ├── parser.h              # Command tokenizer and structured command representations
│   ├── executor.h            # Command dispatch (built-ins, foreground/background)
│   ├── builtins.h            # Built-in command registry and execution interface
│   ├── pipeline.h            # Multi-stage pipe orchestration
│   ├── redirection.h         # File descriptor redirection (<, >, >>)
│   ├── signals.h             # SIGINT, SIGTSTP, and SIGCHLD handlers
│   ├── history.h             # Persistent history buffer and file I/O
│   ├── autocomplete.h        # Raw-mode TAB completion engine
│   └── utils.h               # Safe string, whitespace, and path helpers
├── src/                      # Modular C++17 implementations
│   ├── main.cpp              # Entry point
│   ├── shell.cpp             # Interactive REPL, prompt rendering, and raw line editor
│   ├── parser.cpp            # Semicolon splitting, operator normalization, and parsing
│   ├── executor.cpp          # Process spawning, process groups, and synchronization
│   ├── builtins.cpp          # Native cd, echo, pwd, ls, pinfo, search, history, exit
│   ├── pipeline.cpp          # Pipe creation, descriptor wiring, and wait loop
│   ├── redirection.cpp       # open()/dup2() I/O setup (0644 creation mode)
│   ├── signals.cpp           # Signal installation and foreground forwarding
│   ├── history.cpp           # Cross-session history loading, ring shift, and saving
│   ├── autocomplete.cpp      # Built-in, PATH, and directory completion logic
│   └── utils.cpp             # Shared utility routines
├── docs/
│   └── ARCHITECTURE.md       # In-depth technical architecture & lifecycle guide
├── Makefile                  # Strict C++17 build system (-Wall -Wextra -Werror -pedantic)
├── LICENSE                   # MIT License
└── README.md
```

### Execution Flow

```text
  User Input (Raw Terminal Mode)
        │
        ├──▶ [TAB]        ──▶ Autocomplete Engine (Built-ins / PATH / CWD)
        ├──▶ [Up / Down]  ──▶ Persistent History Navigator
        │
        ▼
  Command Parser (Semicolons, Quotes, Pipes, Redirections, '&')
        │
        ▼
  Command Executor
        ├──▶ Single Built-in  ──▶ Save/Restore FDs ──▶ Execute in Parent Process
        ├──▶ Single External  ──▶ fork() + setpgid() + Redirection ──▶ execvp()
        └──▶ Pipeline         ──▶ pipe() chain + fork() per stage + dup2()
        │
        ▼
  Signal & Process Manager (SIGINT / SIGTSTP / SIGCHLD reaping)
```

For a deeper look at process-group isolation, descriptor management, and terminal mode handling, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Quick Start

### Prerequisites

- A POSIX-compliant operating system (**Linux** recommended for full `/proc` support, or **macOS**)
- A C++17-compatible compiler (`g++` or `clang++`)
- GNU `make`

### Build & Run

```bash
# Compile with strict warnings (-Wall -Wextra -Werror -std=c++17 -pedantic)
make

# Launch the interactive shell
./posix-shell
```

### Build Targets

| Command | Action |
| :--- | :--- |
| `make` | Compiles all sources into `build/` and links `./posix-shell` |
| `make run` | Builds (if needed) and starts `./posix-shell` |
| `make clean` | Removes `build/` object files and the `./posix-shell` binary |
| `make rebuild` | Performs a full clean rebuild |

## Interactive Usage Examples

### Navigation & Built-in Commands

```bash
<rajath@host:~> pwd
/home/rajath/posix-shell

<rajath@host:~> ls -la src
<rajath@host:~> cd src
<rajath@host:~/src> cd -
<rajath@host:~> echo "Hello from POSIX Shell"
Hello from POSIX Shell

<rajath@host:~> search Makefile
True
```

### Foreground & Background Execution

```bash
# Foreground execution (shell waits for completion)
<rajath@host:~> sleep 2

# Background execution (prints child PID and returns immediately)
<rajath@host:~> sleep 10 &
58375
<rajath@host:~> sleep 5 & echo "running concurrently"
58376
running concurrently
```

### I/O Redirection & Multi-Stage Pipelines

```bash
# Truncate and append redirection
<rajath@host:~> echo "first line" > notes.txt
<rajath@host:~> echo "second line" >> notes.txt
<rajath@host:~> cat < notes.txt

# Arbitrary-length pipelines combined with redirection
<rajath@host:~> ls src | sort | head -n 5 > top_files.txt
<rajath@host:~> cat < notes.txt | wc -l > line_count.txt
```

### Process Inspection (`pinfo`)

```bash
# Inspect the shell process or any target PID (Linux /proc)
<rajath@host:~> pinfo
pid -- 4210
Process Status -- S+
memory -- 67854 {Virtual Memory}
Executable Path -- ~/posix-shell

<rajath@host:~> pinfo 7777
```

### Autocomplete & History

- Press **`TAB`** to auto-complete built-ins (`ec<TAB>` -> `echo`), `PATH` binaries, local executables (`./po<TAB>`), or files and directories (`cd in<TAB>` -> `cd include/`). When multiple matches exist, the shell expands their longest common prefix and lists all candidates.
- Press **`Up` / `Down`** arrows to browse persistent command history and edit recalled commands inline before pressing `Enter`.
- Run `history` to view up to the 10 most recent commands, or `history <n>` (up to 20) to display a specific count.

### Signals

- **`Ctrl+C`** (`SIGINT`) interrupts the active foreground job without exiting the shell.
- **`Ctrl+Z`** (`SIGTSTP`) suspends the active foreground job without suspending the shell.
- **`Ctrl+D`** (`EOF`) exits the shell cleanly and restores terminal settings.

## Design Principles & System Calls

This shell deliberately avoids high-level wrappers and external dependencies:

- **No `system()`, `popen()`, or `pclose()`** — all process spawning and IPC are orchestrated directly with `fork()`, `execvp()`, `pipe()`, `dup2()`, and `waitpid()`.
- **No `ncurses` or `readline`** — interactive line editing, backspace handling, arrow-key escape sequences, and `TAB` completion are built directly on `termios` (`tcgetattr()` / `tcsetattr()`), `read()`, and `write()`.
- **No STL or `<filesystem>`** — directory traversal, metadata inspection, and string handling rely strictly on POSIX/C interfaces (`opendir()`, `readdir()`, `stat()`, `lstat()`, `access()`, `getcwd()`, `chdir()`).
- **Strict Compilation** — builds cleanly under `-Wall -Wextra -Werror -std=c++17 -pedantic`.

## Platform Compatibility

- **Linux**: Full support for all features, including `pinfo` via the `/proc/<pid>` virtual filesystem.
- **macOS**: Compiles and runs all shell features (built-ins, job control, redirection, pipelines, signals, autocomplete, and persistent history). Because macOS does not expose a Linux-style `/proc` filesystem, `pinfo` requires a Linux environment.

## Author & License

**Rajath Gadagkar** (`rajathgadagkar@gmail.com`)

Distributed under the [MIT License](LICENSE).
