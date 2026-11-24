#ifndef BUILTINS_H
#define BUILTINS_H

#include "shell.h"
#include "parser.h"

int is_builtin(const char *name);
int execute_builtin(Shell *shell, SimpleCommand *cmd, int allow_cd);

#endif
