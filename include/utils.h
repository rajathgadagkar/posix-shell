#ifndef UTILS_H
#define UTILS_H

#include <limits.h>

char *dup_string(const char *s);
void trim_spaces(char *s);
int is_blank(const char *s);
void make_display_path(const char *cwd, const char *home, char *out, int out_size);
void safe_strcpy(char *dst, const char *src, int size);
void safe_strcat(char *dst, const char *src, int size);
int path_join(const char *a, const char *b, char *out, int size);

#endif
