#include "utils.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

char *dup_string(const char *s){
    if(!s) return NULL;
    size_t n=strlen(s); char *p=new char[n+1]; strcpy(p,s); return p;
}
void trim_spaces(char *s){
    if(!s) return;
    int n=(int)strlen(s), a=0;
    while(a<n && (s[a]==' '||s[a]=='\t'||s[a]=='\n'||s[a]=='\r')) a++;
    int b=n-1; while(b>=a && (s[b]==' '||s[b]=='\t'||s[b]=='\n'||s[b]=='\r')) b--;
    int j=0; for(int i=a;i<=b;i++) s[j++]=s[i]; s[j]='\0';
}
int is_blank(const char *s){ if(!s) return 1; for(int i=0;s[i];i++) if(s[i]!=' '&&s[i]!='\t'&&s[i]!='\n'&&s[i]!='\r') return 0; return 1; }
void safe_strcpy(char *dst,const char *src,int size){ if(size<=0) return; if(!src) src=""; snprintf(dst,size,"%s",src); }
void safe_strcat(char *dst,const char *src,int size){ if(size<=0) return; int n=(int)strlen(dst); if(n<size-1) snprintf(dst+n,size-n,"%s",src?src:""); }
void make_display_path(const char *cwd,const char *home,char *out,int out_size){
    if(strcmp(cwd,home)==0){ safe_strcpy(out,"~",out_size); return; }
    int h=(int)strlen(home);
    if(strncmp(cwd,home,h)==0 && cwd[h]=='/') snprintf(out,out_size,"~%s",cwd+h);
    else safe_strcpy(out,cwd,out_size);
}
int path_join(const char *a,const char *b,char *out,int size){
    if(!a||!b) return -1;
    if(b[0]=='/') { safe_strcpy(out,b,size); return 0; }
    snprintf(out,size,"%s/%s",a,b); return 0;
}
