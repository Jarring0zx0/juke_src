#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 4996)
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) void jukeOut(const char* s);

#ifdef __cplusplus
}
#endif

#ifdef OUTWRAPPER_IMPL
static bool jukeOut__ensure_console(){
	static bool tried=false;
	if(GetConsoleWindow()) return true;
	if(tried) return false;
	tried=true;
	if(AttachConsole(ATTACH_PARENT_PROCESS)) return GetConsoleWindow()!=nullptr;
	const char* pid=std::getenv("JUKE_CONSOLE_PID");
	if(pid&&*pid){
		char* e=nullptr;
		unsigned long v=std::strtoul(pid,&e,10);
		if(e!=pid&&v) AttachConsole((DWORD)v);
	}
	return GetConsoleWindow()!=nullptr;
}
static bool jukeOut__pipe_write(const char* s){
	static HANDLE hp=nullptr;
	const char* name="\\\\.\\pipe\\JukePipe";
	const char* msg=s?s:"";
	auto wr=[&](){
		DWORD w=0;
		BOOL ok=WriteFile(hp,msg,(DWORD)std::strlen(msg),&w,nullptr);
		if(!ok) return false;
		FlushFileBuffers(hp);
		return true;
	};

	if(hp && hp!=INVALID_HANDLE_VALUE){
		if(wr()) return true;
		CloseHandle(hp); hp=nullptr;
	}

	hp=CreateFileA(name,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
	if(hp!=INVALID_HANDLE_VALUE){
		if(wr()) return true;
		CloseHandle(hp); hp=nullptr;
	}
	return false;
}
__declspec(dllexport) void jukeOut(const char* s){
	if(jukeOut__pipe_write(s)) return;
	if(!jukeOut__ensure_console()) return;
	HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
	if(!h||h==INVALID_HANDLE_VALUE) return;
	DWORD m=0;
	if(GetConsoleMode(h,&m)) SetConsoleMode(h,m|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	auto wra=[&](const char* t){
		DWORD wr=0;
		WriteConsoleA(h,t,(DWORD)std::strlen(t),&wr,nullptr);
	};
	const char* msg=s?s:"";
	wra("    \x1b[38;2;64;64;64m[\x1b[38;2;82;15;15m-\x1b[38;2;64;64;64m] ");
	wra(msg);
	wra("\x1b[0m\r\n");
}
#endif
