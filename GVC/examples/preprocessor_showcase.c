#pragma once

/*
   Comprehensive preprocessor showcase for this compiler.
   Demonstrates:
   - #include (direct and macro-expanded)
   - #define / #undef
   - object-like, function-like, and variadic macros
   - # and ## operators
   - #if/#ifdef/#ifndef/#elif/#else/#endif and defined()
   - #line
   - builtins: __FILE__, __LINE__, __DATE__, __TIME__, __COUNTER__,
               __STDC__, __STDC_VERSION__, __STDC_HOSTED__
   - #warning and #error (inside disabled blocks)
*/

extern int printf(char* s, ...);

#define HEADER_PATH "pp_showcase_header.h"
#include HEADER_PATH

#define BASE 10
#undef BASE
#define BASE 21

#define ADD(x, y) ((x) + (y))
#define SQR(x) ((x) * (x))
#define STR(x) #x
#define CAT(a, b) a##b

#define LOG printf
#define TRACE(fmt, ...) LOG(fmt , ## __VA_ARGS__)
#define FORWARD(...) __VA_ARGS__

#if defined(FROM_HEADER) && (FROM_HEADER == 77)
int CAT(global_, flag) = 1;
#elif defined FROM_HEADER
int CAT(global_, flag) = 2;
#else
#error FROM_HEADER is missing or has an unexpected value
#endif

#ifdef BASE
int base_is_defined = 1;
#else
int base_is_defined = 0;
#endif

#ifndef NOT_DEFINED_YET
#define NOT_DEFINED_YET 42
#endif

#if 0
#warning this warning is intentionally disabled
#error this error is intentionally disabled
#endif

#line 500 "virtual_showcase.c"
int remapped_line = __LINE__;

#line 1 "examples/preprocessor_showcase.c"

int main()
{
    int c0 = __COUNTER__;
    int c1 = __COUNTER__;

    int sum = FORWARD(ADD(BASE, HEADER_NUMBER));
    int squared = SQR(5);
    char* macro_text = STR(preprocessor_ok);
    int pasted_value = CAT(global_, flag);

    char* file = __FILE__;
    char* date = __DATE__;
    char* time = __TIME__;

    int stdc = __STDC__;
    int stdc_version = __STDC_VERSION__;
    int stdc_hosted = __STDC_HOSTED__;

    TRACE("showcase: %s\n", macro_text);
    TRACE("value=%d sum=%d squared=%d\n", pasted_value, sum, squared);
    TRACE("counter=%d,%d stdc=%d stdc_ver=%d hosted=%d\n", c0, c1, stdc, stdc_version, stdc_hosted);
    TRACE("file=%s date=%s time=%s line=%d\n", file, date, time, remapped_line);
    TRACE("trace with no variadic arguments\n");

    if (base_is_defined != 1) return 1;
    if (pasted_value != 1) return 2;
    if (NOT_DEFINED_YET != 42) return 3;
    if (remapped_line != 500) return 4;
    if (stdc != 1) return 5;
    if (stdc_hosted != 1) return 6;
    if (c1 <= c0) return 7;

    return 0;
}
