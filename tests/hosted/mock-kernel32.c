// Mock kernel32 so runtime/windows.ll can be JIT-tested on Linux.
#include <string.h>
#include <stdint.h>

static char out_buf[65536]; static int out_len = 0;
static char in_buf[65536];  static int in_len = 0, in_pos = 0;
static short last_attr = 7;
static int cleared_cells = 0, cursor_homed = 0;
static int csbi_should_fail = 0;

void* GetStdHandle(int n){ return (void*)(intptr_t)(n==-11?1:2); }

int WriteFile(void* h,const void* b,int n,int* wrote,void* o){
    (void)h;(void)o; if(out_len+n<(int)sizeof(out_buf)){memcpy(out_buf+out_len,b,n);out_len+=n;}
    if(wrote)*wrote=n; return 1;
}
int ReadFile(void* h,void* b,int n,int* got,void* o){
    (void)h;(void)o; int k=0;
    while(k<n && in_pos<in_len) ((char*)b)[k++]=in_buf[in_pos++];
    if(got)*got=k; return 1;
}
int SetConsoleTextAttribute(void* h,short a){ (void)h; last_attr=a; return 1; }
int GetConsoleScreenBufferInfo(void* h,short* info){
    (void)h; if(csbi_should_fail) return 0;
    info[0]=80; info[1]=25; info[2]=0; info[3]=0; info[4]=last_attr;
    for(int i=5;i<11;i++) info[i]=0;
    return 1;
}
int FillConsoleOutputCharacterA(void* h,char c,int n,int coord,int* w){
    (void)h;(void)c;(void)coord; cleared_cells=n; if(w)*w=n; return 1;
}
int FillConsoleOutputAttribute(void* h,short a,int n,int coord,int* w){
    (void)h;(void)coord;(void)n; last_attr=a; if(w)*w=n; return 1;
}
int SetConsoleCursorPosition(void* h,int coord){ (void)h;(void)coord; cursor_homed=1; return 1; }

// test harness accessors
void mock_reset(void){ out_len=0; in_len=0; in_pos=0; last_attr=7; cleared_cells=0; cursor_homed=0; csbi_should_fail=0; }
void mock_set_input(const char* s,int n){ memcpy(in_buf,s,n); in_len=n; in_pos=0; }
int  mock_out(char* dst){ memcpy(dst,out_buf,out_len); return out_len; }
short mock_attr(void){ return last_attr; }
int  mock_cleared(void){ return cleared_cells; }
int  mock_homed(void){ return cursor_homed; }
void mock_fail_csbi(int v){ csbi_should_fail=v; }
