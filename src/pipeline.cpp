#include "pipeline.h"
#include "builtins.h"
#include "redirection.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

int execute_pipeline(Shell *s, ParsedCommand *p){
    int n=p->count; if(n<=0) return 0;
    int pipes[MAX_CMDS-1][2];
    pid_t pids[MAX_CMDS]; pid_t pgid=-1;
    for(int i=0;i<n-1;i++) if(pipe(pipes[i])<0){ perror("pipe"); return 1; }
    for(int i=0;i<n;i++){
        pid_t pid=fork();
        if(pid<0){ perror("fork"); for(int k=0;k<n-1;k++){close(pipes[k][0]);close(pipes[k][1]);} return 1; }
        if(pid==0){
            signal(SIGINT,SIG_DFL); signal(SIGTSTP,SIG_DFL); signal(SIGCHLD,SIG_DFL);
            if(pgid<0) setpgid(0,0); else setpgid(0,pgid);
            if(i>0) dup2(pipes[i-1][0],STDIN_FILENO);
            if(i<n-1) dup2(pipes[i][1],STDOUT_FILENO);
            for(int k=0;k<n-1;k++){ close(pipes[k][0]); close(pipes[k][1]); }
            if(apply_redirections(&p->commands[i])!=0) _exit(1);
            if(is_builtin(p->commands[i].argv[0])) { int rc=execute_builtin(s,&p->commands[i],0); fflush(NULL); _exit(rc); }
            execvp(p->commands[i].argv[0],p->commands[i].argv);
            fprintf(stderr,"%s: command not found\n",p->commands[i].argv[0]); _exit(127);
        }
        if(pgid<0) pgid=pid;
        setpgid(pid,pgid); pids[i]=pid;
    }
    for(int k=0;k<n-1;k++){close(pipes[k][0]);close(pipes[k][1]);}
    if(p->background){ printf("%d\n",(int)pgid); fflush(stdout); return 0; }
    s->foreground_pid=pgid; s->foreground_pgid=pgid; s->foreground_active=1; s->foreground_stopped=0;
    sigset_t block_set, old_set;
    sigemptyset(&block_set); sigaddset(&block_set,SIGCHLD);
    sigprocmask(SIG_BLOCK,&block_set,&old_set);
    int stopped=0;
    for(int i=0;i<n;i++){
        int status; while(waitpid(pids[i],&status,WUNTRACED)<0 && errno==EINTR){}
        if(WIFSTOPPED(status)) stopped=1;
    }
    sigprocmask(SIG_SETMASK,&old_set,NULL);
    s->foreground_stopped=stopped; s->foreground_active=0; s->foreground_pid=-1; s->foreground_pgid=-1;
    return 0;
}
