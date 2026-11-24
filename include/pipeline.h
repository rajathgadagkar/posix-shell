#ifndef PIPELINE_H
#define PIPELINE_H
#include "shell.h"
#include "parser.h"
int execute_pipeline(Shell *shell, ParsedCommand *p);
#endif
