#ifndef GUI_FIXEDPOINT_H
#define GUI_FIXEDPOINT_H

#include <climits>
#include <cstdint>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wuseless-cast"
#endif
extern "C" {
#include "../driver/FixedMath/Fixed64.h"
}
#pragma GCC diagnostic pop

#endif
