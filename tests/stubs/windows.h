// Minimal stand-in for <windows.h>, ONLY so the Windows glue can be syntax-checked on Linux.
#pragma once
#include <cstddef>
#include <cstdint>
typedef void* LPVOID; typedef void* HMODULE; typedef void* HWND; typedef void* HANDLE; typedef void* HGLOBAL;
typedef unsigned int UINT; typedef uintptr_t WPARAM; typedef intptr_t LPARAM; typedef intptr_t LRESULT;
typedef unsigned long DWORD; typedef int BOOL; typedef wchar_t WCHAR;
#define __stdcall
#define __declspec(x)
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define CP_UTF8 65001
#define CF_UNICODETEXT 13
#define GMEM_MOVEABLE 2
#define DLL_PROCESS_ATTACH 1
int MultiByteToWideChar(UINT, DWORD, const char*, int, wchar_t*, int);
int WideCharToMultiByte(UINT, DWORD, const wchar_t*, int, char*, int, const char*, BOOL*);
HGLOBAL GlobalAlloc(UINT, size_t); HGLOBAL GlobalFree(HGLOBAL); void* GlobalLock(HGLOBAL); BOOL GlobalUnlock(HGLOBAL);
BOOL OpenClipboard(HWND); BOOL CloseClipboard(); BOOL EmptyClipboard();
HANDLE SetClipboardData(UINT, HANDLE); HANDLE GetClipboardData(UINT);
