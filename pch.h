// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

#include <Windows.h>
#include <profileapi.h>

// add headers that you want to pre-compile here
#include "ue.h"
#include "Other/JSON/json.hpp"

#include <fstream>
#include <chrono>
#include <iostream>
#include <utility>
#include <deque>


//#define USE_SOL
#ifdef USE_SOL
#include <sol/sol.hpp>
#endif

#include "SDK.h"

#include "detours.h"

//#include "sigscanner.h"

#endif //PCH_H
