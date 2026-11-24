#ifndef SHELL_H
#define SHELL_H

#include <limits.h>
#include <sys/types.h>

#define SHELL_MAX_INPUT 8192
#define MAX_ARGS 256
#define MAX_CMDS 128
#define HISTORY_MAX 20
#define HISTORY_DISPLAY_MAX 10

class Shell {
public:
    Shell();
    ~Shell();
    bool initialize();
    void run();
    void print_prompt();
    int read_line(char *buffer, int size);

    char home[PATH_MAX];
    char previous_dir[PATH_MAX];
    pid_t foreground_pid;
    pid_t foreground_pgid;
    int foreground_active;
    int foreground_stopped;
    int should_exit;

    char history[HISTORY_MAX][SHELL_MAX_INPUT];
    int history_count;
    char history_file[PATH_MAX];
};

extern Shell *g_shell;

#endif
