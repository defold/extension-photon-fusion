// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#ifdef PHOTON_DEBUG
#include <cstdio>
#include <cstdlib>
#define PHOTON_ASSERT(condition, message)     \
    do {                                      \
        if (!(condition)) {                   \
            std::fprintf(stderr,              \
                "PHOTON_ASSERT failed: %s\n"  \
                "  %s:%d\n",                  \
                message, __FILE__, __LINE__); \
            std::abort();                     \
        }                                     \
    } while (0)
#else
#define PHOTON_ASSERT(condition, message)
#endif
