#ifndef GVC_COMPAT_STDATOMIC_H
#define GVC_COMPAT_STDATOMIC_H

/*
 * Minimal compatibility shim for basic atomic syntax support.
 * This does not provide lock-free/thread-safe semantics.
 */

typedef int atomic_int;

#define atomic_init(obj, val) (*(obj) = (val))
#define atomic_load(obj) (*(obj))

#endif
