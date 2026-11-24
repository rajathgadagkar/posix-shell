#ifndef HISTORY_H
#define HISTORY_H
#include "shell.h"
void history_load(const char *file,char hist[][SHELL_MAX_INPUT],int *count);
void history_save(const char *file,char hist[][SHELL_MAX_INPUT],int count);
void history_add(const char *file,char hist[][SHELL_MAX_INPUT],int *count,const char *line);
int history_print(char hist[][SHELL_MAX_INPUT],int count,int num,int invalid);
#endif
