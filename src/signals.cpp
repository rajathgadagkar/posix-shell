#include "signals.h"
#include "shell.h"
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static void sigint_handler(int){
    if(g_shell && g_shell->foreground_active && g_shell->foreground_pgid>0) kill(-g_shell->foreground_pgid,SIGINT);
}
static void sigtstp_handler(int){
    if(g_shell && g_shell->foreground_active && g_shell->foreground_pgid>0) kill(-g_shell->foreground_pgid,SIGTSTP);
}
static void sigchld_handler(int){
    while(waitpid(-1,NULL,WNOHANG)>0){}
}
void install_signal_handlers(){
    signal(SIGINT,sigint_handler); signal(SIGTSTP,sigtstp_handler); signal(SIGCHLD,sigchld_handler);
}
