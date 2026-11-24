#include "autocomplete.h"
#include "shell.h"
#include "utils.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

#define MAX_COMPLETIONS 2048

static void redraw_line(
    const char *buf
) {
    printf("\r\033[2K");

    if (g_shell) {
        g_shell->print_prompt();
    }

    printf(
        "%s",
        buf
    );

    fflush(stdout);
}

static int starts_with(
    const char *s,
    const char *prefix
) {
    size_t plen =
        strlen(prefix);

    return strncmp(
        s,
        prefix,
        plen
    ) == 0;
}

static void sort_names(
    char names[][PATH_MAX],
    int n
) {
    for (
        int i = 1;
        i < n;
        i++
    ) {
        char key[PATH_MAX];

        safe_strcpy(
            key,
            names[i],
            sizeof(key)
        );

        int j = i - 1;

        while (
            j >= 0 &&
            strcmp(
                names[j],
                key
            ) > 0
        ) {
            memmove(
                names[j + 1],
                names[j],
                sizeof(names[j + 1])
            );

            j--;
        }

        safe_strcpy(
            names[j + 1],
            key,
            sizeof(names[j + 1])
        );
    }
}

static int dedup_names(
    char names[][PATH_MAX],
    int n
) {
    if (n <= 1) {
        return n;
    }

    int write = 1;

    for (
        int read = 1;
        read < n;
        read++
    ) {
        if (
            strcmp(
                names[read],
                names[write - 1]
            ) != 0
        ) {
            if (
                write != read
            ) {
                memmove(
                    names[write],
                    names[read],
                    sizeof(names[write])
                );
            }

            write++;
        }
    }

    return write;
}

static void show_matches(
    char names[][PATH_MAX],
    int n
) {
    putchar('\n');

    for (
        int i = 0;
        i < n;
        i++
    ) {
        printf(
            "%s",
            names[i]
        );

        if (
            i != n - 1
        ) {
            printf("  ");
        }
    }

    putchar('\n');
}

/*
 * Split a token into directory and filename prefix.
 *
 * Examples:
 *
 *     abc      -> "."      + "abc"
 *     ./abc    -> "."      + "abc"
 *     dir/abc  -> "dir"    + "abc"
 *     ../abc   -> ".."     + "abc"
 */
static void split_path_prefix(
    const char *token,
    char directory[PATH_MAX],
    char prefix[NAME_MAX + 1]
) {
    const char *slash =
        strrchr(
            token,
            '/'
        );

    if (!slash) {
        snprintf(
            directory,
            PATH_MAX,
            "."
        );

        snprintf(
            prefix,
            NAME_MAX + 1,
            "%s",
            token
        );

        return;
    }

    int dir_len =
        (int)(slash - token);

    if (
        dir_len == 0
    ) {
        snprintf(
            directory,
            PATH_MAX,
            "/"
        );
    }

    else {
        if (
            dir_len >= PATH_MAX
        ) {
            dir_len = PATH_MAX - 1;
        }

        memcpy(
            directory,
            token,
            (size_t)dir_len
        );

        directory[dir_len] = '\0';
    }

    snprintf(
        prefix,
        NAME_MAX + 1,
        "%s",
        slash + 1
    );
}

/*
 * Complete files/directories for the current token.
 *
 * Examples:
 *
 *     alp      -> alpha.txt
 *     ./alp    -> ./alpha.txt
 *     dir/alp  -> dir/alpha.txt
 */
static void complete_files(
    char *buffer,
    int *len,
    int capacity
) {
    int i =
        *len - 1;

    while (
        i >= 0 &&
        buffer[i] != ' ' &&
        buffer[i] != '\t' &&
        buffer[i] != '|' &&
        buffer[i] != '>' &&
        buffer[i] != '<'
    ) {
        i--;
    }

    int start =
        i + 1;

    int token_len =
        *len - start;

    if (
        token_len < 0 ||
        token_len >= PATH_MAX
    ) {
        return;
    }

    char token[PATH_MAX];

    memcpy(
        token,
        buffer + start,
        (size_t)token_len
    );

    token[token_len] = '\0';

    char directory[PATH_MAX];

    char prefix[NAME_MAX + 1];

    split_path_prefix(
        token,
        directory,
        prefix
    );

    DIR *d =
        opendir(
            directory
        );

    if (!d) {
        return;
    }

    /*
     * Static avoids stack overflow.
     */
    static char names[
        MAX_COMPLETIONS
    ][PATH_MAX];

    int n = 0;

    struct dirent *e;

    while (
        (e = readdir(d)) != NULL
    ) {
        if (
            n >= MAX_COMPLETIONS
        ) {
            break;
        }

        if (
            !starts_with(
                e->d_name,
                prefix
            )
        ) {
            continue;
        }

        snprintf(
            names[n],
            sizeof(names[n]),
            "%s",
            e->d_name
        );

        n++;
    }

    closedir(d);

    if (
        n == 0
    ) {
        return;
    }

    sort_names(
        names,
        n
    );

    n =
        dedup_names(
            names,
            n
        );

    /*
     * One match.
     */
    if (
        n == 1
    ) {
        int plen =
            (int)strlen(prefix);

        int add =
            (int)strlen(names[0]) - plen;

        if (
            add <= 0
        ) {
            return;
        }

        if (
            *len + add >= capacity
        ) {
            return;
        }

        strcat(
            buffer,
            names[0] + plen
        );

        *len =
            (int)strlen(buffer);

        printf(
            "%s",
            names[0] + plen
        );

        /*
         * Add '/' for directories.
         */
        char full[PATH_MAX];

        if (
            strcmp(
                directory,
                "."
            ) == 0
        ) {
            snprintf(
                full,
                sizeof(full),
                "%s",
                names[0]
            );
        }

        else {
            path_join(
                directory,
                names[0],
                full,
                sizeof(full)
            );
        }

        struct stat st;

        if (
            lstat(
                full,
                &st
            ) == 0 &&
            S_ISDIR(
                st.st_mode
            )
        ) {
            if (
                *len + 1 < capacity &&
                buffer[*len - 1] != '/'
            ) {
                buffer[*len] = '/';
                buffer[*len + 1] = '\0';

                (*len)++;

                putchar('/');
            }
        }

        fflush(stdout);

        return;
    }

    /*
     * Multiple matches.
     *
     * Find common prefix.
     */
    int plen =
        (int)strlen(prefix);

    int common =
        plen;

    while (1) {
        char c =
            names[0][common];

        if (
            c == '\0'
        ) {
            break;
        }

        int same = 1;

        for (
            int j = 1;
            j < n;
            j++
        ) {
            if (
                names[j][common] != c
            ) {
                same = 0;
                break;
            }
        }

        if (!same) {
            break;
        }

        common++;
    }

    if (
        common > plen
    ) {
        int add =
            common - plen;

        if (
            *len + add < capacity
        ) {
            strncat(
                buffer,
                names[0] + plen,
                (size_t)add
            );

            *len =
                (int)strlen(buffer);

            printf(
                "%.*s",
                add,
                names[0] + plen
            );

            fflush(stdout);

            return;
        }
    }

    /*
     * No more common characters.
     * Show matches.
     */
    show_matches(
        names,
        n
    );

    redraw_line(
        buffer
    );
}

/*
 * Check whether a name is one of our builtins.
 */
static int is_builtin_name(
    const char *name
) {
    const char *builtins[] = {
        "cd",
        "echo",
        "pwd",
        "ls",
        "pinfo",
        "search",
        "history",
        "exit"
    };

    for (
        int i = 0;
        i < 8;
        i++
    ) {
        if (
            strcmp(
                name,
                builtins[i]
            ) == 0
        ) {
            return 1;
        }
    }

    return 0;
}

/*
 * Search PATH directories for executable commands.
 */
static void collect_path_commands(
    const char *prefix,
    char names[][PATH_MAX],
    int *n
) {
    const char *path =
        getenv(
            "PATH"
        );

    if (!path) {
        return;
    }

    char paths[8192];

    snprintf(
        paths,
        sizeof(paths),
        "%s",
        path
    );

    char *save = NULL;

    char *p =
        strtok_r(
            paths,
            ":",
            &save
        );

    while (p) {
        DIR *d =
            opendir(
                p[0]
                    ? p
                    : "."
            );

        if (d) {
            struct dirent *e;

            while (
                (e = readdir(d)) != NULL
            ) {
                if (
                    *n >= MAX_COMPLETIONS
                ) {
                    break;
                }

                if (
                    !starts_with(
                        e->d_name,
                        prefix
                    )
                ) {
                    continue;
                }

                char full[PATH_MAX];

                path_join(
                    p,
                    e->d_name,
                    full,
                    sizeof(full)
                );

                struct stat st;

                if (
                    lstat(
                        full,
                        &st
                    ) != 0
                ) {
                    continue;
                }

                if (
                    S_ISDIR(
                        st.st_mode
                    )
                ) {
                    continue;
                }

                if (
                    access(
                        full,
                        X_OK
                    ) != 0
                ) {
                    continue;
                }

                snprintf(
                    names[*n],
                    sizeof(names[*n]),
                    "%s",
                    e->d_name
                );

                (*n)++;
            }

            closedir(d);
        }

        p =
            strtok_r(
                NULL,
                ":",
                &save
            );
    }
}

/*
 * Complete the first token.
 *
 * This supports:
 *
 *     ec<TAB>  -> echo
 *     she<TAB> -> shell
 *     te<TAB>  -> test_exec
 *
 * It also supports explicit paths:
 *
 *     ./te<TAB> -> ./test_exec
 */
static void complete_first_token(
    char *buffer,
    int *len,
    int capacity
) {
    /*
     * If the token contains '/',
     * use path completion.
     */
    if (
        strchr(
            buffer,
            '/'
        ) != NULL
    ) {
        complete_files(
            buffer,
            len,
            capacity
        );

        return;
    }

    static char names[
        MAX_COMPLETIONS
    ][PATH_MAX];

    int n = 0;

    /*
     * Builtins.
     */
    const char *builtins[] = {
        "cd",
        "echo",
        "pwd",
        "ls",
        "pinfo",
        "search",
        "history",
        "exit"
    };

    int builtin_count = 8;

    for (
        int i = 0;
        i < builtin_count;
        i++
    ) {
        if (
            starts_with(
                builtins[i],
                buffer
            ) &&
            n < MAX_COMPLETIONS
        ) {
            snprintf(
                names[n],
                sizeof(names[n]),
                "%s",
                builtins[i]
            );

            n++;
        }
    }

    /*
     * PATH commands.
     */
    collect_path_commands(
        buffer,
        names,
        &n
    );

    /*
     * Also consider executable files
     * in the current directory.
     *
     * This fixes:
     *
     *     te<TAB>
     *
     * when test_exec exists locally.
     */
    DIR *d =
        opendir(".");

    if (d) {
        struct dirent *e;

        while (
            (e = readdir(d)) != NULL
        ) {
            if (
                n >= MAX_COMPLETIONS
            ) {
                break;
            }

            if (
                !starts_with(
                    e->d_name,
                    buffer
                )
            ) {
                continue;
            }

            if (
                is_builtin_name(
                    e->d_name
                )
            ) {
                continue;
            }

            struct stat st;

            if (
                lstat(
                    e->d_name,
                    &st
                ) != 0
            ) {
                continue;
            }

            if (
                S_ISDIR(
                    st.st_mode
                )
            ) {
                continue;
            }

            if (
                access(
                    e->d_name,
                    X_OK
                ) != 0
            ) {
                continue;
            }

            snprintf(
                names[n],
                sizeof(names[n]),
                "%s",
                e->d_name
            );

            n++;
        }

        closedir(d);
    }

    if (
        n == 0
    ) {
        return;
    }

    sort_names(
        names,
        n
    );

    n =
        dedup_names(
            names,
            n
        );

    int plen =
        (int)strlen(buffer);

    /*
     * Exactly one completion.
     */
    if (
        n == 1
    ) {
        int add =
            (int)strlen(names[0]) - plen;

        if (
            add <= 0
        ) {
            return;
        }

        if (
            *len + add >= capacity
        ) {
            return;
        }

        strcat(
            buffer,
            names[0] + plen
        );

        *len =
            (int)strlen(buffer);

        printf(
            "%s",
            names[0] + plen
        );

        fflush(stdout);

        return;
    }

    /*
     * Multiple completions.
     *
     * Find common prefix.
     */
    int common =
        plen;

    while (1) {
        char c =
            names[0][common];

        if (
            c == '\0'
        ) {
            break;
        }

        int same = 1;

        for (
            int j = 1;
            j < n;
            j++
        ) {
            if (
                names[j][common] != c
            ) {
                same = 0;
                break;
            }
        }

        if (!same) {
            break;
        }

        common++;
    }

    if (
        common > plen
    ) {
        int add =
            common - plen;

        if (
            *len + add < capacity
        ) {
            strncat(
                buffer,
                names[0] + plen,
                (size_t)add
            );

            *len =
                (int)strlen(buffer);

            printf(
                "%.*s",
                add,
                names[0] + plen
            );

            fflush(stdout);

            return;
        }
    }

    /*
     * No common extension.
     * Show all matches.
     */
    show_matches(
        names,
        n
    );

    redraw_line(
        buffer
    );
}

void autocomplete_buffer(
    char *buffer,
    int *len,
    int capacity
) {
    if (
        !buffer ||
        !len
    ) {
        return;
    }

    int i =
        *len - 1;

    /*
     * Find beginning of current token.
     */
    while (
        i >= 0 &&
        buffer[i] != ' ' &&
        buffer[i] != '\t' &&
        buffer[i] != '|' &&
        buffer[i] != '>' &&
        buffer[i] != '<'
    ) {
        i--;
    }

    int start =
        i + 1;

    /*
     * First token = command completion.
     */
    if (
        start == 0
    ) {
        complete_first_token(
            buffer,
            len,
            capacity
        );

        return;
    }

    /*
     * Later tokens = file/directory completion.
     */
    complete_files(
        buffer,
        len,
        capacity
    );
}