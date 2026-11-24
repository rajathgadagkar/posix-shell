#include "redirection.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

int apply_redirections(SimpleCommand *cmd){
    if(cmd->input_file){
        int fd=open(cmd->input_file,O_RDONLY);
        if(fd<0){ perror("input redirection"); return 1; }
        if(dup2(fd,STDIN_FILENO)<0){ perror("dup2"); close(fd); return 1; }
        close(fd);
    }
    if(cmd->output_file){
        int flags=O_WRONLY|O_CREAT|(cmd->append?O_APPEND:O_TRUNC);
        int fd=open(cmd->output_file,flags,0644);
        if(fd<0){ perror("output redirection"); return 1; }
        if(dup2(fd,STDOUT_FILENO)<0){ perror("dup2"); close(fd); return 1; }
        close(fd);
    }
    return 0;
}
