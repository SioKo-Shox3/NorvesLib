#pragma once

/**
 * @file DebugConfig.h
 * @brief Debug/log/profile feature switches.
 */

#ifndef NORVES_BUILD_DEBUG
    #ifdef _DEBUG
        #define NORVES_BUILD_DEBUG 1
    #else
        #define NORVES_BUILD_DEBUG 0
    #endif
#endif

#ifndef NORVES_BUILD_RELEASE
    #if defined(NDEBUG) && !NORVES_BUILD_DEBUG
        #define NORVES_BUILD_RELEASE 1
    #else
        #define NORVES_BUILD_RELEASE 0
    #endif
#endif

#ifndef NORVES_BUILD_DEVELOPMENT
    #if NORVES_BUILD_RELEASE
        #define NORVES_BUILD_DEVELOPMENT 0
    #else
        #define NORVES_BUILD_DEVELOPMENT 1
    #endif
#endif

#ifndef NORVES_ENABLE_LOGGING
    #define NORVES_ENABLE_LOGGING NORVES_BUILD_DEVELOPMENT
#endif

#ifndef NORVES_ENABLE_DEBUG_OUTPUT
    #define NORVES_ENABLE_DEBUG_OUTPUT NORVES_BUILD_DEVELOPMENT
#endif

#ifndef NORVES_ENABLE_STATS
    #define NORVES_ENABLE_STATS NORVES_BUILD_DEVELOPMENT
#endif

#ifndef NORVES_ENABLE_PROFILING
    #define NORVES_ENABLE_PROFILING NORVES_BUILD_DEVELOPMENT
#endif
