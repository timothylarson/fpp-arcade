#pragma once
// Stands in for FPP's log.h in tools/host: info, warnings and errors go to
// the terminal; debug is dropped.

#include <cstdio>

#define VB_PLUGIN 0
#define LogErr(facility, ...) (std::fprintf(stderr, "error: " __VA_ARGS__))
#define LogWarn(facility, ...) (std::fprintf(stderr, "warning: " __VA_ARGS__))
#define LogInfo(facility, ...) (std::fprintf(stderr, __VA_ARGS__))
#define LogDebug(facility, ...) ((void)0)
#define LogExcess(facility, ...) ((void)0)
