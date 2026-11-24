# Architecture Guide

This document details the internal design, module boundaries, and runtime lifecycle of **POSIX Shell (`posix-shell`)**.

## 1. High-Level Module Map

| Module | Header | Source | Responsibility |
| :--- | :--- | :--- | :--- |
| **Entry Point** | — | `src/main.cpp` | Instantiates `Shell`, initializes state, and enters the interactive loop |
| **Shell Core** | `include/shell.h` | `src/shell.cpp` | Prompt formatting, raw-mode terminal line editing, history/autocomplete hooks, and REPL orchestration |
| **Parser** | `include/parser.h` | `src/parser.cpp` | Semicolon splitting, quote-aware operator spacing, tokenization via `strtok()`, and `ParsedCommand` construction |
| **Executor** | `include/executor.h` | `src/executor.cpp` | Command-list sequencing, inline `&` splitting, built-in FD save/restore, external `fork()`/`execvp()`, and foreground `waitpid()` |
| **Built-ins** | `include/builtins.h` | `src/builtins.cpp` | Direct implementations of `cd`, `echo`, `pwd`, `ls`, `pinfo`, `search`, `history`, and `exit` |
| **Pipelines** | `include/pipeline.h` | `src/pipeline.cpp` | N-stage pipe allocation, child process spawning, `dup2()` wiring, descriptor closure, and group synchronization |
| **Redirection** | `include/redirection.h` | `src/redirection.cpp` | Input (`<`), output overwrite (`>`), and append (`>>`) setup using `open()` (`0644`) and `dup2()` |
| **Signals** | `include/signals.h` | `src/signals.cpp` | Installs handlers for `SIGINT`, `SIGTSTP`, and `SIGCHLD` to protect the shell and reap background children |
| **History** | `include/history.h` | `src/history.cpp` | Loads/saves persistent history (up to 20 entries) across sessions and validates `history <n>` arguments |
| **Autocomplete** | `include/autocomplete.h` | `src/autocomplete.cpp` | Completes built-ins, `PATH` executables, relative paths, and directory entries with longest-common-prefix expansion |
| **Utilities** | `include/utils.h` | `src/utils.cpp` | Safe string duplication/copying, whitespace trimming, path joining, and `~` display path formatting |

## 2. Lifecycle of a Command

1. **Prompt & Raw Input (`shell.cpp`)**
   - Resolves username, hostname, and current working directory, shortening the shell's home prefix to `~`.
   - Switches the terminal into non-canonical raw mode (`termios`) to intercept individual keystrokes: printable characters, Backspace, `TAB` (delegated to `autocomplete_buffer()`), `Up`/`Down` ANSI escape sequences (history traversal), and `Ctrl+D` (`EOF`).
   - Restores original terminal attributes before executing commands so interactive child programs behave normally.

2. **Parsing (`parser.cpp`)**
   - Splits the input line on `;` into independent command segments.
   - Normalizes whitespace around `<`, `>`, `>>`, `|`, and `&` outside single/double quotes.
   - Populates a `ParsedCommand` containing up to `MAX_CMDS` `SimpleCommand` structs, each holding its `argv`, optional `input_file`, optional `output_file`, and `append` flag.

3. **Execution & Process Control (`executor.cpp`, `pipeline.cpp`, `redirection.cpp`)**
   - **Built-ins**: Executed inside the parent shell process so state changes (like `cd` or `exit`) persist. Before running, `STDIN`, `STDOUT`, and `STDERR` are duplicated (`dup()`), redirections are applied, `fflush(NULL)` flushes buffered output, and original descriptors are restored via `dup2()`.
   - **External Commands**: Spawned via `fork()`. The child resets signal dispositions to `SIG_DFL`, establishes its own process group (`setpgid(0, 0)`), applies file redirections, and invokes `execvp()`.
   - **Background Jobs (`&`)**: The parent prints the child PID immediately and returns to the prompt; terminated background processes are reaped asynchronously by the `SIGCHLD` handler.
   - **Foreground Jobs**: The parent records `foreground_pid` and `foreground_pgid`, blocks `SIGCHLD` temporarily, and waits via `waitpid(..., WUNTRACED)` so `Ctrl+Z` stops are detected cleanly.
   - **Pipelines**: Creates `N - 1` pipes, forks each stage into a shared pipeline process group, wires `STDIN`/`STDOUT` with `dup2()`, applies per-stage file redirections (overriding pipe ends when specified), closes unused pipe descriptors in both parent and children, and waits for all stages to complete.

4. **Signal Handling (`signals.cpp`)**
   - `SIGINT` (`Ctrl+C`) and `SIGTSTP` (`Ctrl+Z`) are forwarded to the active foreground process group (`-foreground_pgid`) when a foreground job is running; otherwise, the shell stays alive and redraws cleanly.
   - `SIGCHLD` invokes a non-blocking `waitpid(-1, ..., WNOHANG)` loop to prevent zombie processes.

5. **Persistent History (`history.cpp`)**
   - Stored in a history file inside the shell's home directory.
   - Maintains a rolling window of up to `HISTORY_MAX` (20) entries, evicting the oldest command when full, and displaying up to `HISTORY_DISPLAY_MAX` (10) entries by default.
