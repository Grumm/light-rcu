#ifndef _LRCU_TYPES_H
#define _LRCU_TYPES_H

#include "defines.h"

#ifdef LRCU_LINUX
#include <linux/types.h>
#else

#include <inttypes.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#endif

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;

#endif