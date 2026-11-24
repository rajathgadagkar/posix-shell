#include "executor.h"
#include "parser.h"
#include "builtins.h"
#include "redirection.h"
#include "pipeline.h"
#include "signals.h"
#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

#include <signal.h>
#include <errno.h>

/*
 * Free all dynamically allocated parser data.
 */
static void free_parsed(
    ParsedCommand *p
) {
    for (
        int i = 0;
        i < p->count;
        i++
    ) {
        for (
            int j = 0;
            j < p->commands[i].argc;
            j++
        ) {
            delete[] p->commands[i].argv[j];
        }

        if (p->commands[i].input_file)
            delete[] p->commands[i].input_file;

        if (p->commands[i].output_file)
            delete[] p->commands[i].output_file;
    }
}

/*
 * Execute a builtin in the shell process.
 *
 * This is required for commands such as cd because
 * changing directory inside a child would not change
 * the shell's own working directory.
 */
static int run_builtin_parent(
    Shell *s,
    SimpleCommand *c
) {
    int saved_in =
        dup(STDIN_FILENO);

    int saved_out =
        dup(STDOUT_FILENO);

    int saved_err =
        dup(STDERR_FILENO);

    if (
        saved_in < 0 ||
        saved_out < 0 ||
        saved_err < 0
    ) {
        perror("dup");

        if (saved_in >= 0)
            close(saved_in);

        if (saved_out >= 0)
            close(saved_out);

        if (saved_err >= 0)
            close(saved_err);

        return 1;
    }

    int rc =
        apply_redirections(c);

    if (rc == 0) {
        rc =
            execute_builtin(
                s,
                c,
                1
            );
    }

    /*
     * Flush all stdio buffers before restoring
     * stdout/stderr. This is necessary when a
     * builtin has output redirection.
     */
    fflush(NULL);

    if (
        dup2(
            saved_in,
            STDIN_FILENO
        ) < 0
    ) {
        perror("dup2");
    }

    if (
        dup2(
            saved_out,
            STDOUT_FILENO
        ) < 0
    ) {
        perror("dup2");
    }

    if (
        dup2(
            saved_err,
            STDERR_FILENO
        ) < 0
    ) {
        perror("dup2");
    }

    close(saved_in);
    close(saved_out);
    close(saved_err);

    return rc;
}

/*
 * Execute one external command.
 */
static int run_external(
    SimpleCommand *c,
    int background,
    Shell *s
) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    /*
     * Child process.
     */
    if (pid == 0) {
        /*
         * Background/foreground child processes
         * should not use the shell's signal handlers.
         */
        signal(
            SIGINT,
            SIG_DFL
        );

        signal(
            SIGTSTP,
            SIG_DFL
        );

        signal(
            SIGCHLD,
            SIG_DFL
        );

        /*
         * Give the process its own process group.
         * The first process is its own group leader.
         */
        if (
            setpgid(
                0,
                0
            ) < 0
        ) {
            /*
             * setpgid can fail in some environments
             * after fork without making execution
             * itself invalid, so we don't terminate
             * solely because of this.
             */
        }

        /*
         * Apply <, >, >>.
         */
        if (
            apply_redirections(c) != 0
        ) {
            _exit(1);
        }

        /*
         * Execute external program.
         */
        execvp(
            c->argv[0],
            c->argv
        );

        /*
         * execvp() failed.
         */
        fprintf(
            stderr,
            "%s: command not found\n",
            c->argv[0]
        );

        fflush(stderr);

        _exit(127);
    }

    /*
     * Parent process.
     *
     * Place child in its own process group too.
     */
    if (
        setpgid(
            pid,
            pid
        ) < 0
    ) {
        /*
         * Do not terminate the shell simply because
         * the process-group operation races with the
         * child.
         */
    }

    /*
     * BACKGROUND PROCESS
     *
     * We print the PID immediately and return without
     * waiting. The SIGCHLD handler will reap the child.
     */
    if (background) {
        printf(
            "%d\n",
            (int)pid
        );

        fflush(stdout);

        /*
         * Give the shell a chance to print the next
         * prompt before a very short-lived child
         * finishes and writes output.
         */
        usleep(1000);

        return 0;
    }

    /*
     * FOREGROUND PROCESS
     */
    s->foreground_pid = pid;
    s->foreground_pgid = pid;
    s->foreground_active = 1;
    s->foreground_stopped = 0;

    /*
     * Block SIGCHLD while the parent is synchronously
     * waiting for the foreground process.
     */
    sigset_t block_set;
    sigset_t old_set;

    sigemptyset(
        &block_set
    );

    sigaddset(
        &block_set,
        SIGCHLD
    );

    sigprocmask(
        SIG_BLOCK,
        &block_set,
        &old_set
    );

    int status;

    while (1) {
        pid_t w =
            waitpid(
                pid,
                &status,
                WUNTRACED
            );

        if (w < 0) {
            if (errno == EINTR)
                continue;

            perror("waitpid");

            break;
        }

        /*
         * Ctrl-Z caused the process to stop.
         */
        if (WIFSTOPPED(status)) {
            s->foreground_stopped = 1;
            break;
        }

        /*
         * Process finished normally or due to a signal.
         */
        if (
            WIFEXITED(status) ||
            WIFSIGNALED(status)
        ) {
            break;
        }
    }

    sigprocmask(
        SIG_SETMASK,
        &old_set,
        NULL
    );

    s->foreground_active = 0;
    s->foreground_pid = -1;
    s->foreground_pgid = -1;

    return 0;
}

/*
 * Execute one already-parsed command.
 */
static void execute_parsed_command(
    Shell *s,
    ParsedCommand *p
) {
    if (
        p->count <= 0
    ) {
        return;
    }

    /*
     * More than one command means a pipeline.
     */
    if (p->count > 1) {
        execute_pipeline(
            s,
            p
        );

        return;
    }

    SimpleCommand *c =
        &p->commands[0];

    if (c->argc == 0)
        return;

    /*
     * Built-in commands execute directly in the parent
     * shell process; background execution applies to
     * external commands.
     */
    if (
        is_builtin(
            c->argv[0]
        )
    ) {
        run_builtin_parent(
            s,
            c
        );

        return;
    }

    /*
     * Ordinary external command.
     */
    run_external(
        c,
        p->background,
        s
    );
}

/*
 * Find the next '&' outside single/double quotes.
 */
static int find_background_operator(
    const char *s
) {
    int in_sq = 0;
    int in_dq = 0;

    for (
        int i = 0;
        s[i];
        i++
    ) {
        if (
            s[i] == '\'' &&
            !in_dq
        ) {
            in_sq = !in_sq;
            continue;
        }

        if (
            s[i] == '"' &&
            !in_sq
        ) {
            in_dq = !in_dq;
            continue;
        }

        if (
            !in_sq &&
            !in_dq &&
            s[i] == '&'
        ) {
            return i;
        }
    }

    return -1;
}

/*
 * Execute a semicolon-separated portion.
 *
 * This also supports:
 *
 *     command &
 *     command & command
 *     command & command &
 *
 * Example:
 *
 *     sleep 10 & echo hello
 *
 * means:
 *
 *     sleep 10       -> background
 *     echo hello     -> foreground
 */
static void execute_semicolon_part(
    Shell *s,
    char *part
) {
    char *current = part;

    while (
        current &&
        *current &&
        !s->should_exit
    ) {
        trim_spaces(current);

        if (
            current[0] == '\0'
        ) {
            break;
        }

        int pos =
            find_background_operator(
                current
            );

        /*
         * No '&' remains.
         */
        if (pos < 0) {
            ParsedCommand p;

            int rc =
                parse_command(
                    current,
                    &p
                );

            if (
                rc < 0 ||
                p.count <= 0
            ) {
                printf(
                    "Invalid command\n"
                );

                free_parsed(&p);

                return;
            }

            execute_parsed_command(
                s,
                &p
            );

            free_parsed(&p);

            return;
        }

        /*
         * Split at '&'.
         */
        current[pos] = '\0';

        char *next =
            current + pos + 1;

        trim_spaces(current);
        trim_spaces(next);

        /*
         * Execute everything before '&' as a
         * background command.
         */
        if (
            current[0] != '\0'
        ) {
            int len =
                (int)strlen(current);

            /*
             * Add '&' back so parse_command() marks
             * the command as background.
             */
            if (
                len <
                SHELL_MAX_INPUT - 2
            ) {
                current[len] = ' ';
                current[len + 1] = '&';
                current[len + 2] = '\0';
            }

            ParsedCommand p;

            int rc =
                parse_command(
                    current,
                    &p
                );

            if (
                rc < 0 ||
                p.count <= 0
            ) {
                printf(
                    "Invalid command\n"
                );

                free_parsed(&p);

                return;
            }

            execute_parsed_command(
                s,
                &p
            );

            free_parsed(&p);
        }

        /*
         * Continue with the command after '&'.
         */
        current = next;
    }
}

void execute_input_line(
    Shell *s,
    char *input
) {
    char *parts[MAX_CMDS];

    int n =
        split_semicolon(
            input,
            parts,
            MAX_CMDS
        );

    for (
        int i = 0;
        i < n &&
        !s->should_exit;
        i++
    ) {
        execute_semicolon_part(
            s,
            parts[i]
        );
    }
}