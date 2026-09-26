#define NOMINMAX
#include <windows.h>
#include <vector>
#include <string>
int main(){auto in=GetStdHandle(STD_INPUT_HANDLE),out=GetStdHandle(STD_OUTPUT_HANDLE);auto read=[&](void* p,DWORD n){auto b=(BYTE*)p;while(n){DWORD got=0;if(!ReadFile(in,b,n,&got,nullptr)||!got)return false;b+=got;n-=got;}return true;};auto write=[&](const void* p,DWORD n){auto b=(const BYTE*)p;while(n){DWORD got=0;if(!WriteFile(out,b,n,&got,nullptr)||!got)return false;b+=got;n-=got;}return true;};for(;;){uint32_t n=0;if(!read(&n,4)||!n)break;if(n>1024*1024)return 2;std::string text(n,0);if(!read(text.data(),n))break;if(text=="crash")return 73;if(text=="hang"){for(;;)Sleep(1000);}if(text=="where"){wchar_t path[32768];GetCurrentDirectoryW(32768,path);int size=WideCharToMultiByte(CP_UTF8,0,path,-1,nullptr,0,nullptr,nullptr);text.resize(size);WideCharToMultiByte(CP_UTF8,0,path,-1,text.data(),size,nullptr,nullptr);text.resize(size-1);}else text="helper:"+text;n=(uint32_t)text.size();if(!write(&n,4)||!write(text.data(),n))return 3;}return 0;}

