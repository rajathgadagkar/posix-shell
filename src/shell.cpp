#include "shell.h"
#include "parser.h"
#include "executor.h"
#include "history.h"
#include "autocomplete.h"
#include "signals.h"
#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <pwd.h>

#include <termios.h>
#include <errno.h>

Shell *g_shell = NULL;

Shell::Shell() {
    home[0] = '\0';
    previous_dir[0] = '\0';
    history_file[0] = '\0';

    foreground_pid = -1;
    foreground_pgid = -1;
    foreground_active = 0;
    foreground_stopped = 0;
    should_exit = 0;
    history_count = 0;

    for (int i = 0; i < HISTORY_MAX; i++)
        history[i][0] = '\0';
}

Shell::~Shell() {
}

bool Shell::initialize() {
    /*
     * The directory from which the shell is invoked is
     * the shell's home directory.
     */
    if (
        getcwd(
            home,
            sizeof(home)
        ) == NULL
    ) {
        perror("getcwd");
        return false;
    }

    /*
     * Initially, the previous directory is also the
     * startup directory.
     */
    if (
        getcwd(
            previous_dir,
            sizeof(previous_dir)
        ) == NULL
    ) {
        perror("getcwd");
        return false;
    }

    /*
     * History file.
     *
     * Do not use OLDPWD or PWD.
     */
    const char *home_env =
        getenv("HOME");

    if (
        home_env &&
        home_env[0] != '\0'
    ) {
        snprintf(
            history_file,
            sizeof(history_file),
            "%s/.shell_history",
            home_env
        );
    } else {
        struct passwd *pw =
            getpwuid(
                getuid()
            );

        if (!pw)
            return false;

        snprintf(
            history_file,
            sizeof(history_file),
            "%s/.shell_history",
            pw->pw_dir
        );
    }

    history_load(
        history_file,
        history,
        &history_count
    );

    g_shell = this;

    install_signal_handlers();

    return true;
}

void Shell::print_prompt() {
    char cwd[PATH_MAX];
    char display[PATH_MAX];
    char hostname[256];

    struct passwd *pw =
        getpwuid(
            getuid()
        );

    const char *username =
        pw
            ? pw->pw_name
            : "unknown";

    if (
        gethostname(
            hostname,
            sizeof(hostname)
        ) != 0
    ) {
        snprintf(
            hostname,
            sizeof(hostname),
            "unknown"
        );
    }

    hostname[
        sizeof(hostname) - 1
    ] = '\0';

    if (
        !getcwd(
            cwd,
            sizeof(cwd)
        )
    ) {
        safe_strcpy(
            cwd,
            "?",
            sizeof(cwd)
        );
    }

    make_display_path(
        cwd,
        home,
        display,
        sizeof(display)
    );

    printf(
        "<%s@%s:%s> ",
        username,
        hostname,
        display
    );

    fflush(stdout);
}

static void restore_terminal(
    struct termios *old_termios
) {
    tcsetattr(
        STDIN_FILENO,
        TCSANOW,
        old_termios
    );
}

static void clear_current_line(
    const char *buffer
) {
    /*
     * Move to beginning and erase entire line.
     */
    printf(
        "\r\033[2K"
    );

    if (g_shell)
        g_shell->print_prompt();

    printf(
        "%s",
        buffer
    );

    fflush(stdout);
}

int Shell::read_line(
    char *buffer,
    int size
) {
    struct termios oldt;
    struct termios raw;

    /*
     * We need character-by-character input for:
     *
     * - TAB
     * - UP
     * - DOWN
     * - CTRL-C
     * - CTRL-D
     * - Backspace
     */
    if (
        tcgetattr(
            STDIN_FILENO,
            &oldt
        ) == -1
    ) {
        /*
         * Fallback for non-interactive input.
         */
        if (
            !fgets(
                buffer,
                size,
                stdin
            )
        ) {
            return -1;
        }

        int n =
            (int)strlen(buffer);

        if (
            n > 0 &&
            buffer[n - 1] == '\n'
        ) {
            buffer[n - 1] = '\0';
        }

        return 0;
    }

    raw = oldt;

    /*
     * Disable canonical mode and terminal echo.
     */
    raw.c_lflag &=
        ~(ICANON | ECHO);

    /*
     * Let read() return one character at a time.
     */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (
        tcsetattr(
            STDIN_FILENO,
            TCSANOW,
            &raw
        ) == -1
    ) {
        return -1;
    }

    buffer[0] = '\0';

    int len = 0;

    /*
     * history_position means:
     *
     * 0 ... history_count-1 = actual history entry
     * history_count          = empty/current line
     *
     * We initialize it to the empty/current state.
     */
    int history_position =
        history_count;

    int navigating_history = 0;

    /*
     * Remember original line before history navigation.
     *
     * If the user enters text and then presses UP,
     * the original text can be discarded as normal shell
     * behavior. We don't need a persistent scratch buffer.
     */
    char c;

    while (1) {
        ssize_t r =
            read(
                STDIN_FILENO,
                &c,
                1
            );

        if (r <= 0) {
            restore_terminal(&oldt);
            return -1;
        }

        /*
         * CTRL-D
         */
        if (c == 4) {
            if (len == 0) {
                putchar('\n');

                restore_terminal(&oldt);

                return -1;
            }

            /*
             * With text already present, ignore CTRL-D.
             */
            continue;
        }

        /*
         * ENTER
         */
        if (
            c == '\n' ||
            c == '\r'
        ) {
            buffer[len] = '\0';

            putchar('\n');

            restore_terminal(&oldt);

            return 0;
        }

        /*
         * CTRL-C while typing.
         *
         * This should affect only the current input line,
         * not terminate the shell.
         */
        if (c == 3) {
            printf(
                "^C\n"
            );

            buffer[0] = '\0';

            restore_terminal(&oldt);

            return 0;
        }

        /*
         * BACKSPACE
         */
        if (
            c == 127 ||
            c == 8
        ) {
            if (len > 0) {
                len--;

                buffer[len] = '\0';

                printf(
                    "\b \b"
                );

                fflush(stdout);
            }

            /*
             * Input has changed, so autocomplete's
             * repeated-TAB state must not continue.
             */
            continue;
        }

        /*
         * TAB
         */
        if (c == '\t') {
            autocomplete_buffer(
                buffer,
                &len,
                size
            );

            continue;
        }

        /*
         * ESCAPE sequence.
         *
         * We specifically support:
         *
         * ESC [ A -> UP
         * ESC [ B -> DOWN
         */
        if (c == 27) {
            char seq1;
            char seq2;

            if (
                read(
                    STDIN_FILENO,
                    &seq1,
                    1
                ) != 1
            ) {
                continue;
            }

            if (
                seq1 != '['
            ) {
                continue;
            }

            if (
                read(
                    STDIN_FILENO,
                    &seq2,
                    1
                ) != 1
            ) {
                continue;
            }

            /*
             * UP
             */
            if (seq2 == 'A') {
                if (history_count == 0)
                    continue;

                /*
                 * Move one command backward.
                 *
                 * Do not go past the oldest entry.
                 */
                if (
                    history_position >
                    0
                ) {
                    history_position--;
                }

                navigating_history = 1;

                if (
                    history_position >= 0 &&
                    history_position <
                    history_count
                ) {
                    safe_strcpy(
                        buffer,
                        history[
                            history_position
                        ],
                        size
                    );

                    len =
                        (int)strlen(buffer);
                }

                clear_current_line(
                    buffer
                );

                continue;
            }

            /*
             * DOWN
             */
            if (seq2 == 'B') {
                if (
                    !navigating_history ||
                    history_count == 0
                ) {
                    continue;
                }

                /*
                 * Move toward newer commands.
                 */
                if (
                    history_position <
                    history_count
                ) {
                    history_position++;
                }

                /*
                 * history_count means the empty
                 * new input line.
                 */
                if (
                    history_position >=
                    history_count
                ) {
                    history_position =
                        history_count;

                    buffer[0] = '\0';
                    len = 0;
                }

                else {
                    safe_strcpy(
                        buffer,
                        history[
                            history_position
                        ],
                        size
                    );

                    len =
                        (int)strlen(buffer);
                }

                clear_current_line(
                    buffer
                );

                continue;
            }

            continue;
        }

        /*
         * Printable character.
         */
        if (
            (unsigned char)c >= 32
        ) {
            if (
                len < size - 1
            ) {
                buffer[len++] = c;

                buffer[len] = '\0';

                putchar(c);

                fflush(stdout);

                /*
                 * Once the user changes the line,
                 * history navigation is reset.
                 */
                history_position =
                    history_count;

                navigating_history = 0;
            }
        }
    }
}

void Shell::run() {
    char input[SHELL_MAX_INPUT];

    while (!should_exit) {
        print_prompt();

        int rc =
            read_line(
                input,
                sizeof(input)
            );

        /*
         * CTRL-D / EOF.
         */
        if (rc < 0)
            break;

        trim_spaces(input);

        if (input[0] == '\0')
            continue;

        /*
         * Store the complete entered line in history.
         */
        history_add(
            history_file,
            history,
            &history_count,
            input
        );

        execute_input_line(
            this,
            input
        );
    }

    history_save(
        history_file,
        history,
        history_count
    );
}