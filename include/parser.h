#ifndef PARSER_H
#define PARSER_H

#include "shell.h"

enum TokenType { TOK_WORD, TOK_PIPE, TOK_LT, TOK_GT, TOK_APPEND, TOK_AMP };

struct SimpleCommand {
    char *argv[MAX_ARGS];
    int argc;
    char *input_file;
    char *output_file;
    int append;
};

struct ParsedCommand {
    SimpleCommand commands[MAX_CMDS];
    int count;
    int background;
};

void init_simple(SimpleCommand *cmd);
int split_semicolon(char *input, char *parts[], int max_parts);
int parse_command(char *command, ParsedCommand *out);

#endif
