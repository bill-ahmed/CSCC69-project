#include "threads/fixed-point.h"

int add_integer(int f, int n)
{
    return f + (n * SHIFT_FACTOR);
}

int subtract_integer(int f, int n)
{
    return f - (n * SHIFT_FACTOR);
}

int mult_fp(int f, int g)
{
    return ((int64_t) f) * (g / SHIFT_FACTOR);
}

int div_fp(int f, int g)
{
    return (((int64_t) f) * SHIFT_FACTOR) / g;
}

int convert_int(int f)
{
    return f / SHIFT_FACTOR;
}

int convert_int_nearest(int f)
{
    return f >= 0 ? convert_int(f + (SHIFT_FACTOR >> 1)) : convert_int(f - (SHIFT_FACTOR >> 1));
}

int convert_fp(int n)
{
    return n * SHIFT_FACTOR;
}
