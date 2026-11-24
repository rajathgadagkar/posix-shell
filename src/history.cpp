#include "history.h"
#include "utils.h"
#include <cstdio>
#include <cstring>

void history_load(const char *file,char hist[][SHELL_MAX_INPUT],int *count){
    *count=0; FILE *f=fopen(file,"r"); if(!f) return; char line[SHELL_MAX_INPUT];
    while(fgets(line,sizeof(line),f)){
        int n=(int)strlen(line); while(n>0&&(line[n-1]=='\n'||line[n-1]=='\r')) line[--n]='\0';
        if(line[0]=='\0') continue;
        if(*count<HISTORY_MAX) safe_strcpy(hist[*count],line,SHELL_MAX_INPUT),(*count)++;
        else { for(int i=1;i<HISTORY_MAX;i++) safe_strcpy(hist[i-1],hist[i],SHELL_MAX_INPUT); safe_strcpy(hist[HISTORY_MAX-1],line,SHELL_MAX_INPUT); }
    }
    fclose(f);
}
void history_save(const char *file,char hist[][SHELL_MAX_INPUT],int count){
    FILE *f=fopen(file,"w"); if(!f) return; for(int i=0;i<count;i++) fprintf(f,"%s\n",hist[i]); fclose(f);
}
void history_add(const char *file,char hist[][SHELL_MAX_INPUT],int *count,const char *line){
    if(!line||line[0]=='\0') return;
    if(*count<HISTORY_MAX) safe_strcpy(hist[*count],line,SHELL_MAX_INPUT),(*count)++;
    else { for(int i=1;i<HISTORY_MAX;i++) safe_strcpy(hist[i-1],hist[i],SHELL_MAX_INPUT); safe_strcpy(hist[HISTORY_MAX-1],line,SHELL_MAX_INPUT); }
    history_save(file,hist,*count);
}
int history_print(char hist[][SHELL_MAX_INPUT],int count,int num,int invalid){
    if(invalid || num<0){ printf("Invalid arguments\n"); return 1; }
    if(num>count) num=count;
    if(num<0) num=0;
    int start=count-num; if(start<0) start=0; for(int i=start;i<count;i++) printf("%s\n",hist[i]); return 0;
}
