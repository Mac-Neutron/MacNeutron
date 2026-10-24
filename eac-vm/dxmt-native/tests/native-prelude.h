// Ahead of each of DXMT's test programs built natively (build-tests.sh): the Win32 calls the PE build takes from
// mingw's kernel32 (events, waits, the clock, Sleep), from native DXMT's own header, so the programs' events are the
// handles DXMT's fences set. GUIDs need nothing: the native headers make them constexpr.
#pragma once
#include <windows.h>
#include <win32_native.h>
#ifndef GENERIC_ALL
#define GENERIC_ALL 0x10000000u // winnt.h's, which win32_native.h's GENERIC_READ and GENERIC_WRITE stand beside
#endif
