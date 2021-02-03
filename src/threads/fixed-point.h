#include <stdint.h>

#ifndef __THREAD_FIXED_H
#define __THREAD_FIXED_H

/* Take last 14 bits in signed 32-bit integer as fractional component. */
#define SHIFT_FACTOR 16384

/* Add natural number N to fixed-point number F. */
int add_integer(int f, int n);

/* Subtract natural number N from fixed-point number F. */
int subtract_integer(int f, int n);

/* Add two fixed-point numbers. */
int add_fp(int f, int g);

/* Subtract two fixed-point numbers. */
int sub_fp(int f, int g);

/* Multiply two fixed-point numbers. */
int mult_fp(int f, int g);

/* Divide two fixed-point numbers. */
int div_fp(int f, int g);

/* Convert fixed-point number to an integer, rounding towards zero. */
int convert_int(int f);

/* Convert fixed-point to integer, rounding to nearest integer. */
int convert_int_nearest(int f);

/* Convert an integer to fixed-point. */
int convert_fp(int n);

#endif
