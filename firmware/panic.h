#ifndef PANIC_H
#define PANIC_H

// This file is also included by Assembler, so it should only contain preprocessor directives.

// This macro will pre-declare the a3d_panic function with __attribute__((noreturn)) __printflike(0, 1).
#define PICO_PANIC_FUNCTION verbose_panic

#endif // ! define PANIC_H
