#include "builtins.h"
#include "history.h"
#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <pwd.h>
#include <grp.h>

#include <time.h>
#include <fcntl.h>
#include <limits.h>

static int builtin_cd(
    Shell *s,
    SimpleCommand *c
) {
    if (c->argc > 2) {
        printf("Invalid arguments\n");
        return 1;
    }

    char target[PATH_MAX];

    /*
     * cd
     * cd ~
     */
    if (
        c->argc == 1 ||
        strcmp(c->argv[1], "~") == 0
    ) {
        safe_strcpy(
            target,
            s->home,
            sizeof(target)
        );
    }

    /*
     * cd -
     */
    else if (
        strcmp(c->argv[1], "-") == 0
    ) {
        safe_strcpy(
            target,
            s->previous_dir,
            sizeof(target)
        );
    }

    /*
     * cd <directory>
     * cd .
     * cd ..
     */
    else {
        safe_strcpy(
            target,
            c->argv[1],
            sizeof(target)
        );
    }

    char old[PATH_MAX];

    if (
        !getcwd(
            old,
            sizeof(old)
        )
    ) {
        perror("getcwd");
        return 1;
    }

    if (
        chdir(target) != 0
    ) {
        perror("cd");
        return 1;
    }

    /*
     * Save directory before the successful cd.
     * This is our own replacement for OLDPWD.
     */
    safe_strcpy(
        s->previous_dir,
        old,
        sizeof(s->previous_dir)
    );

    /*
     * The real shell's "cd -" normally prints the
     * directory it switched to.
     */
    if (
        c->argc == 2 &&
        strcmp(c->argv[1], "-") == 0
    ) {
        char now[PATH_MAX];

        if (
            getcwd(
                now,
                sizeof(now)
            )
        ) {
            printf(
                "%s\n",
                now
            );
        }
    }

    return 0;
}

static int builtin_echo(
    SimpleCommand *c
) {
    for (
        int i = 1;
        i < c->argc;
        i++
    ) {
        if (i > 1)
            putchar(' ');

        printf(
            "%s",
            c->argv[i]
        );
    }

    putchar('\n');

    return 0;
}

static int builtin_pwd() {
    char cwd[PATH_MAX];

    if (
        !getcwd(
            cwd,
            sizeof(cwd)
        )
    ) {
        perror("pwd");
        return 1;
    }

    printf(
        "%s\n",
        cwd
    );

    return 0;
}

/*
 * Convert file mode bits into the familiar ls -l
 * permissions string.
 */
static void mode_string(
    mode_t m,
    char out[11]
) {
    out[0] =
        S_ISDIR(m) ? 'd' :
        S_ISLNK(m) ? 'l' :
        S_ISCHR(m) ? 'c' :
        S_ISBLK(m) ? 'b' :
        S_ISSOCK(m) ? 's' :
        S_ISFIFO(m) ? 'p' :
        '-';

    const int bits[9] = {
        S_IRUSR,
        S_IWUSR,
        S_IXUSR,
        S_IRGRP,
        S_IWGRP,
        S_IXGRP,
        S_IROTH,
        S_IWOTH,
        S_IXOTH
    };

    const char chars[9] = {
        'r', 'w', 'x',
        'r', 'w', 'x',
        'r', 'w', 'x'
    };

    for (
        int i = 0;
        i < 9;
        i++
    ) {
        out[i + 1] =
            (m & bits[i])
                ? chars[i]
                : '-';
    }

    /*
     * Set-user-ID.
     */
    if (m & S_ISUID) {
        out[3] =
            (out[3] == 'x')
                ? 's'
                : 'S';
    }

    /*
     * Set-group-ID.
     */
    if (m & S_ISGID) {
        out[6] =
            (out[6] == 'x')
                ? 's'
                : 'S';
    }

    /*
     * Sticky bit.
     */
    if (m & S_ISVTX) {
        out[9] =
            (out[9] == 'x')
                ? 't'
                : 'T';
    }

    out[10] = '\0';
}

/*
 * Print one entry in long format.
 */
static void print_long_entry(
    const char *name,
    const char *full
) {
    struct stat st;

    if (
        lstat(
            full,
            &st
        ) != 0
    ) {
        perror(name);
        return;
    }

    char mode[11];

    mode_string(
        st.st_mode,
        mode
    );

    struct passwd *pw =
        getpwuid(
            st.st_uid
        );

    struct group *gr =
        getgrgid(
            st.st_gid
        );

    char tbuf[64];

    struct tm tmv;

    localtime_r(
        &st.st_mtime,
        &tmv
    );

    strftime(
        tbuf,
        sizeof(tbuf),
        "%b %e %H:%M",
        &tmv
    );

    printf(
        "%s %3lu %-8s %-8s %8lld %s %s\n",
        mode,
        (unsigned long)st.st_nlink,
        pw ? pw->pw_name : "?",
        gr ? gr->gr_name : "?",
        (long long)st.st_size,
        tbuf,
        name
    );
}

/*
 * List one file or directory.
 */
static int ls_one(
    const char *path,
    int all,
    int lng,
    int print_header
) {
    struct stat st;

    if (
        lstat(
            path,
            &st
        ) != 0
    ) {
        perror("ls");
        return 1;
    }

    /*
     * If path is a file, print the file itself.
     */
    if (
        !S_ISDIR(st.st_mode)
    ) {
        if (lng) {
            print_long_entry(
                path,
                path
            );
        }

        else {
            printf(
                "%s\n",
                path
            );
        }

        return 0;
    }

    DIR *d =
        opendir(path);

    if (!d) {
        perror("ls");
        return 1;
    }

    char names[4096][NAME_MAX + 1];

    int n = 0;

    struct dirent *e;

    while (
        (e = readdir(d)) != NULL
    ) {
        /*
         * Without -a, hidden files are skipped.
         */
        if (
            !all &&
            e->d_name[0] == '.'
        ) {
            continue;
        }

        if (
            n < 4096
        ) {
            safe_strcpy(
                names[n],
                e->d_name,
                sizeof(names[n])
            );

            n++;
        }
    }

    closedir(d);

    /*
     * Sort entries alphabetically.
     *
     * STL is not used.
     */
    for (
        int i = 1;
        i < n;
        i++
    ) {
        char key[NAME_MAX + 1];

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
            safe_strcpy(
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

    /*
     * Print total blocks for -l.
     */
    if (lng) {
        long long blocks = 0;

        for (
            int i = 0;
            i < n;
            i++
        ) {
            char full[PATH_MAX];

            path_join(
                path,
                names[i],
                full,
                sizeof(full)
            );

            if (
                lstat(
                    full,
                    &st
                ) == 0
            ) {
                blocks += st.st_blocks;
            }
        }

        /*
         * Linux/macOS st_blocks are 512-byte blocks,
         * while ls traditionally reports 1K blocks.
         */
        printf(
            "total %lld\n",
            blocks / 2
        );
    }

    for (
        int i = 0;
        i < n;
        i++
    ) {
        char full[PATH_MAX];

        path_join(
            path,
            names[i],
            full,
            sizeof(full)
        );

        if (lng) {
            print_long_entry(
                names[i],
                full
            );
        }

        else {
            printf(
                "%s\n",
                names[i]
            );
        }
    }

    /*
     * print_header is kept in the interface for
     * multiple-directory support.
     */
    (void)print_header;

    return 0;
}

/*
 * Builtin ls.
 *
 * Supported:
 *
 *   ls
 *   ls -a
 *   ls -l
 *   ls -al
 *   ls -la
 *   ls -a -l
 *   ls -l -a
 *   ls <path>
 *   ls <path1> <path2>
 *   ls -la <path>
 *   ls -l <dir1> -a <dir2>
 *
 * Also supports:
 *
 *   ls ~
 *   ls ~/directory
 */
static int builtin_ls(
    Shell *s,
    SimpleCommand *c
) {
    int all = 0;
    int lng = 0;

    /*
     * Paths are copied here because "~" needs
     * expansion before calling ls_one().
     */
    char paths[MAX_ARGS][PATH_MAX];

    int np = 0;

    for (
        int i = 1;
        i < c->argc;
        i++
    ) {
        char *a =
            c->argv[i];

        /*
         * Process flags.
         */
        if (
            a[0] == '-' &&
            a[1] != '\0'
        ) {
            /*
             * Treat a lone "-" as a path.
             */
            if (
                strcmp(
                    a,
                    "-"
                ) == 0
            ) {
                if (
                    np >= MAX_ARGS
                ) {
                    printf(
                        "Too many path arguments\n"
                    );

                    return 1;
                }

                safe_strcpy(
                    paths[np],
                    a,
                    sizeof(paths[np])
                );

                np++;

                continue;
            }

            for (
                int j = 1;
                a[j];
                j++
            ) {
                if (a[j] == 'a') {
                    all = 1;
                }

                else if (a[j] == 'l') {
                    lng = 1;
                }

                else {
                    printf(
                        "ls: invalid option -- '%c'\n",
                        a[j]
                    );

                    return 1;
                }
            }
        }

        /*
         * Process path operands.
         */
        else {
            if (
                np >= MAX_ARGS
            ) {
                printf(
                    "Too many path arguments\n"
                );

                return 1;
            }

            /*
             * Expand "~".
             */
            if (
                strcmp(
                    a,
                    "~"
                ) == 0
            ) {
                safe_strcpy(
                    paths[np],
                    s->home,
                    sizeof(paths[np])
                );
            }

            /*
             * Also support "~/foo".
             */
            else if (
                strncmp(
                    a,
                    "~/",
                    2
                ) == 0
            ) {
                snprintf(
                    paths[np],
                    sizeof(paths[np]),
                    "%s/%s",
                    s->home,
                    a + 2
                );
            }

            else {
                safe_strcpy(
                    paths[np],
                    a,
                    sizeof(paths[np])
                );
            }

            np++;
        }
    }

    /*
     * No argument means current directory.
     */
    if (
        np == 0
    ) {
        safe_strcpy(
            paths[np],
            ".",
            sizeof(paths[np])
        );

        np++;
    }

    /*
     * List every requested path.
     */
    for (
        int i = 0;
        i < np;
        i++
    ) {
        if (
            np > 1
        ) {
            printf(
                "%s:\n",
                paths[i]
            );
        }

        ls_one(
            paths[i],
            all,
            lng,
            np > 1
        );

        if (
            i != np - 1
        ) {
            putchar('\n');
        }
    }

    return 0;
}

/*
 * pinfo [pid]
 *
 * Linux implementation based on /proc.
 */
static int builtin_pinfo(
    SimpleCommand *c,
    Shell *s
) {
    if (
        c->argc > 2
    ) {
        printf(
            "Invalid arguments\n"
        );

        return 1;
    }

    pid_t pid =
        (c->argc == 2)
            ? (pid_t)atoi(c->argv[1])
            : getpid();

    char path[PATH_MAX];

    char state = '?';

    unsigned long vmsize = 0;

    /*
     * Read /proc/<pid>/status.
     */
    snprintf(
        path,
        sizeof(path),
        "/proc/%d/status",
        (int)pid
    );

    FILE *f =
        fopen(
            path,
            "r"
        );

    if (!f) {
        perror("pinfo");
        return 1;
    }

    char line[512];

    while (
        fgets(
            line,
            sizeof(line),
            f
        )
    ) {
        /*
         * Process state.
         *
         * Example:
         *
         * State:  S (sleeping)
         */
        if (
            strncmp(
                line,
                "State:",
                6
            ) == 0
        ) {
            char st;

            if (
                sscanf(
                    line,
                    "State:\t%c",
                    &st
                ) == 1
            ) {
                state = st;
            }
        }

        /*
         * Virtual memory size.
         */
        else if (
            strncmp(
                line,
                "VmSize:",
                7
            ) == 0
        ) {
            unsigned long x;

            if (
                sscanf(
                    line,
                    "VmSize:%lu",
                    &x
                ) == 1
            ) {
                vmsize = x;
            }
        }
    }

    fclose(f);

    /*
     * Foreground marker.
     *
     * The shell itself is the foreground process when
     * pinfo is executed as a builtin.
     */
    int plus = 0;

    if (
        pid == getpid()
    ) {
        plus = 1;
    }

    /*
     * A currently tracked foreground child is also
     * considered foreground.
     */
    else if (
        s->foreground_active &&
        pid == s->foreground_pid
    ) {
        plus = 1;
    }

    printf(
        "Process Status -- %c%s\n",
        state,
        plus ? "+" : ""
    );

    printf(
        "memory -- %lu\n",
        vmsize
    );

    /*
     * Read executable path through /proc/<pid>/exe.
     */
    snprintf(
        path,
        sizeof(path),
        "/proc/%d/exe",
        (int)pid
    );

    char exe[PATH_MAX];

    ssize_t n =
        readlink(
            path,
            exe,
            sizeof(exe) - 1
        );

    if (
        n < 0
    ) {
        printf(
            "Executable Path -- Unknown\n"
        );
    }

    else {
        exe[n] = '\0';

        char disp[PATH_MAX];

        make_display_path(
            exe,
            s->home,
            disp,
            sizeof(disp)
        );

        printf(
            "Executable Path -- %s\n",
            disp
        );
    }

    return 0;
}

/*
 * Recursive search helper.
 */
static int search_recursive(
    const char *dir,
    const char *target,
    int *found
) {
    DIR *dp =
        opendir(dir);

    if (!dp)
        return 0;

    struct dirent *e;

    while (
        (e = readdir(dp)) != NULL
    ) {
        /*
         * Do not recursively enter "." or "..".
         */
        if (
            strcmp(
                e->d_name,
                "."
            ) == 0 ||
            strcmp(
                e->d_name,
                ".."
            ) == 0
        ) {
            continue;
        }

        /*
         * Check current entry.
         */
        if (
            strcmp(
                e->d_name,
                target
            ) == 0
        ) {
            *found = 1;

            closedir(dp);

            return 1;
        }

        char full[PATH_MAX];

        path_join(
            dir,
            e->d_name,
            full,
            sizeof(full)
        );

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
                search_recursive(
                    full,
                    target,
                    found
                )
            ) {
                closedir(dp);

                return 1;
            }
        }
    }

    closedir(dp);

    return 0;
}

/*
 * search <name>
 */
static int builtin_search(
    SimpleCommand *c
) {
    if (
        c->argc != 2
    ) {
        printf(
            "Invalid arguments\n"
        );

        return 1;
    }

    int found = 0;

    /*
     * Search starting from current directory.
     */
    search_recursive(
        ".",
        c->argv[1],
        &found
    );

    printf(
        found
            ? "True\n"
            : "False\n"
    );

    return 0;
}

/*
 * Parse a non-negative decimal integer.
 *
 * We do not use atoi() for history because atoi("abc")
 * returns 0, which would incorrectly make "history abc"
 * behave like "history 0".
 */
static int parse_nonnegative_integer(
    const char *s,
    int *value
) {
    if (
        !s ||
        !s[0]
    ) {
        return 0;
    }

    long number = 0;

    for (
        int i = 0;
        s[i];
        i++
    ) {
        if (
            s[i] < '0' ||
            s[i] > '9'
        ) {
            return 0;
        }

        number =
            number * 10 +
            (s[i] - '0');

        /*
         * Keep result inside int range.
         */
        if (
            number > 2147483647L
        ) {
            return 0;
        }
    }

    *value = (int)number;

    return 1;
}

/*
 * Return whether command is one of our builtins.
 */
int is_builtin(
    const char *name
) {
    if (!name)
        return 0;

    return
        strcmp(
            name,
            "cd"
        ) == 0 ||

        strcmp(
            name,
            "echo"
        ) == 0 ||

        strcmp(
            name,
            "pwd"
        ) == 0 ||

        strcmp(
            name,
            "ls"
        ) == 0 ||

        strcmp(
            name,
            "pinfo"
        ) == 0 ||

        strcmp(
            name,
            "search"
        ) == 0 ||

        strcmp(
            name,
            "history"
        ) == 0 ||

        strcmp(
            name,
            "exit"
        ) == 0;
}

/*
 * Execute a builtin command.
 */
int execute_builtin(
    Shell *s,
    SimpleCommand *c,
    int allow_cd
) {
    if (
        c->argc == 0
    ) {
        return 0;
    }

    /*
     * cd
     */
    if (
        strcmp(
            c->argv[0],
            "cd"
        ) == 0
    ) {
        /*
         * Builtin cd is executed in the shell process
         * whenever called from the parent.
         */
        return allow_cd
            ? builtin_cd(s, c)
            : builtin_cd(s, c);
    }

    /*
     * echo
     */
    if (
        strcmp(
            c->argv[0],
            "echo"
        ) == 0
    ) {
        return builtin_echo(c);
    }

    /*
     * pwd
     */
    if (
        strcmp(
            c->argv[0],
            "pwd"
        ) == 0
    ) {
        return builtin_pwd();
    }

    /*
     * ls
     */
    if (
        strcmp(
            c->argv[0],
            "ls"
        ) == 0
    ) {
        return builtin_ls(
            s,
            c
        );
    }

    /*
     * pinfo
     */
    if (
        strcmp(
            c->argv[0],
            "pinfo"
        ) == 0
    ) {
        return builtin_pinfo(
            c,
            s
        );
    }

    /*
     * search
     */
    if (
        strcmp(
            c->argv[0],
            "search"
        ) == 0
    ) {
        return builtin_search(c);
    }

    /*
     * history
     */
    if (
        strcmp(
            c->argv[0],
            "history"
        ) == 0
    ) {
        /*
         * history
         */
        if (
            c->argc == 1
        ) {
            return history_print(
                s->history,
                s->history_count,
                HISTORY_DISPLAY_MAX,
                0
            );
        }

        /*
         * history <num>
         */
        if (
            c->argc == 2
        ) {
            int num = 0;

            if (
                !parse_nonnegative_integer(
                    c->argv[1],
                    &num
                )
            ) {
                printf(
                    "Invalid arguments\n"
                );

                return 1;
            }

            return history_print(
                s->history,
                s->history_count,
                num,
                0
            );
        }

        /*
         * history with more than one argument after
         * the command.
         */
        printf(
            "Invalid arguments\n"
        );

        return 1;
    }

    /*
     * exit
     */
    if (
        strcmp(
            c->argv[0],
            "exit"
        ) == 0
    ) {
        s->should_exit = 1;

        return 0;
    }

    return -1;
}