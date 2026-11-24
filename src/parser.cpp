#include "parser.h"
#include "utils.h"

#include <cstring>

void init_simple(SimpleCommand *cmd) {
    cmd->argc = 0;

    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->append = 0;

    for (int i = 0; i < MAX_ARGS; i++)
        cmd->argv[i] = NULL;
}

int split_semicolon(
    char *input,
    char *parts[],
    int max_parts
) {
    int n = 0;

    /*
     * Tokenize semicolon-separated command lists using strtok().
     */
    char *p = strtok(
        input,
        ";\n"
    );

    while (
        p &&
        n < max_parts
    ) {
        parts[n++] = p;

        p = strtok(
            NULL,
            ";\n"
        );
    }

    return n;
}

static int tokenize_with_operators(
    char *src,
    char *tokens[],
    int max_tokens
) {
    /*
     * We insert whitespace around operators so that
     * commands such as:
     *
     *     ls|grep txt
     *     cat<file
     *     echo hi>file
     *     sleep 10& echo hello
     *
     * can all be tokenized correctly using strtok().
     */
    char normalized[SHELL_MAX_INPUT * 2];

    int ni = 0;

    int in_sq = 0;
    int in_dq = 0;

    for (
        int i = 0;
        src[i] &&
        ni < (int)sizeof(normalized) - 4;
        i++
    ) {
        char c = src[i];

        /*
         * Track single and double quotes so quoted arguments
         * (such as echo strings) preserve embedded operators.
         */
        if (
            c == '\'' &&
            !in_dq
        ) {
            in_sq = !in_sq;
            continue;
        }

        if (
            c == '"' &&
            !in_sq
        ) {
            in_dq = !in_dq;
            continue;
        }

        if (!in_sq && !in_dq) {
            /*
             * >> must remain one token.
             */
            if (c == '>') {
                normalized[ni++] = ' ';
                normalized[ni++] = '>';

                if (src[i + 1] == '>') {
                    normalized[ni++] = '>';
                    i++;
                }

                normalized[ni++] = ' ';

                continue;
            }

            /*
             * All single-character shell operators
             * receive whitespace around them.
             */
            if (
                c == '<' ||
                c == '|' ||
                c == '&'
            ) {
                normalized[ni++] = ' ';
                normalized[ni++] = c;
                normalized[ni++] = ' ';

                continue;
            }
        }

        normalized[ni++] = c;
    }

    normalized[ni] = '\0';

    int n = 0;

    /*
     * Split normalized input on whitespace using strtok().
     */
    char *p = strtok(
        normalized,
        " \t\n"
    );

    while (
        p &&
        n < max_tokens - 1
    ) {
        tokens[n++] = dup_string(p);

        p = strtok(
            NULL,
            " \t\n"
        );
    }

    tokens[n] = NULL;

    return n;
}

static void free_tokens(
    char *tokens[],
    int n
) {
    for (int i = 0; i < n; i++)
        delete[] tokens[i];
}

int parse_command(
    char *command,
    ParsedCommand *out
) {
    out->count = 0;
    out->background = 0;

    for (int i = 0; i < MAX_CMDS; i++)
        init_simple(
            &out->commands[i]
        );

    trim_spaces(command);

    if (command[0] == '\0')
        return 0;

    /*
     * Detect '&' at the END of this command.
     *
     * Example:
     *
     *     sleep 10 &
     *
     * becomes a background command.
     *
     * Internal '&' is handled by executor.cpp so
     * that:
     *
     *     sleep 10 & echo hello
     *
     * becomes two commands.
     */
    int L = (int)strlen(command);

    if (L > 0) {
        int j = L - 1;

        while (
            j >= 0 &&
            (
                command[j] == ' ' ||
                command[j] == '\t'
            )
        ) {
            j--;
        }

        if (
            j >= 0 &&
            command[j] == '&'
        ) {
            out->background = 1;

            command[j] = '\0';

            trim_spaces(command);
        }
    }

    char *tokens[MAX_ARGS * 4];

    int nt = tokenize_with_operators(
        command,
        tokens,
        MAX_ARGS * 4
    );

    if (nt == 0)
        return 0;

    int ci = 0;

    SimpleCommand *cur =
        &out->commands[ci];

    for (int i = 0; i < nt; i++) {
        char *t = tokens[i];

        /*
         * Pipe:
         *
         * command1 | command2
         */
        if (
            strcmp(
                t,
                "|"
            ) == 0
        ) {
            if (cur->argc == 0) {
                free_tokens(
                    tokens,
                    nt
                );

                return -1;
            }

            cur->argv[cur->argc] = NULL;

            if (++ci >= MAX_CMDS) {
                free_tokens(
                    tokens,
                    nt
                );

                return -1;
            }

            init_simple(
                &out->commands[ci]
            );

            cur =
                &out->commands[ci];
        }

        /*
         * Input/output redirection.
         */
        else if (
            strcmp(t, "<") == 0 ||
            strcmp(t, ">") == 0 ||
            strcmp(t, ">>") == 0
        ) {
            if (i + 1 >= nt) {
                free_tokens(
                    tokens,
                    nt
                );

                return -1;
            }

            char *file =
                tokens[++i];

            if (
                strcmp(
                    t,
                    "<"
                ) == 0
            ) {
                /*
                 * If multiple input redirections occur,
                 * the last one wins.
                 */
                if (cur->input_file)
                    delete[] cur->input_file;

                cur->input_file =
                    dup_string(file);
            }

            else {
                /*
                 * If multiple output redirections occur,
                 * the last one wins.
                 */
                if (cur->output_file)
                    delete[] cur->output_file;

                cur->output_file =
                    dup_string(file);

                cur->append =
                    (
                        strcmp(
                            t,
                            ">>"
                        ) == 0
                    );
            }
        }

        /*
         * '&' occurring inside an already isolated
         * command should never reach this point for
         * the normal "command &" case because the
         * trailing '&' was removed above.
         *
         * We keep this handling for robustness.
         */
        else if (
            strcmp(
                t,
                "&"
            ) == 0
        ) {
            out->background = 1;
        }

        else {
            if (
                cur->argc >=
                MAX_ARGS - 1
            ) {
                free_tokens(
                    tokens,
                    nt
                );

                return -1;
            }

            cur->argv[cur->argc++] =
                dup_string(t);
        }
    }

    if (cur->argc == 0) {
        free_tokens(
            tokens,
            nt
        );

        return -1;
    }

    cur->argv[cur->argc] = NULL;

    out->count = ci + 1;

    free_tokens(
        tokens,
        nt
    );

    return 0;
}