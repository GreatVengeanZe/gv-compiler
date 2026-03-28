#ifndef GVC_COMPAT_COMPLEX_H
#define GVC_COMPAT_COMPLEX_H

/*
 * Minimal compatibility shim for compilers that do not implement C complex arithmetic.
 * This is intentionally lightweight and only targets common compile-time usage.
 */

#define complex
#define _Complex
#define _Imaginary
#define I 0.0

#define creal(x) (x)
#define cimag(x) (0.0)

#endif
