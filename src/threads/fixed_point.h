#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <debug.h>
#include <stdint.h>

/* Q value. */
#define FIX_Q 16

/* F value. */
#define FIX_F (1 << FIX_Q)

/* Basic definitions of fixed point. */
typedef int fixed_t;

/* Convert int to fixed point. */
static inline fixed_t
int_to_fixed_point (int n)
{
  return n * FIX_F;
}

/* Convert fixed point to int. */
static inline fixed_t
fixed_point_to_int (fixed_t x)
{
  return x >= 0 ? ((x + FIX_F / 2) / FIX_F) : ((x - FIX_F / 2) / FIX_F);
}

/* Addition */
static inline fixed_t
add (fixed_t x, fixed_t y)
{
  return x + y;
}

/* Subtraction */
static inline fixed_t
subtract (fixed_t x, fixed_t y)
{
  return x - y;
}

/* Multiplication */
static inline fixed_t
multiply (fixed_t x, fixed_t y)
{
  return ((int64_t) x) * y / FIX_F;
}

/* Division */
static inline fixed_t
divide (fixed_t x, fixed_t y)
{
  return ((int64_t) x) * FIX_F / y;
}

/* Addition with int */
static inline fixed_t
add_int (fixed_t x, int n)
{
  return add (x, int_to_fixed_point (n));
}

/* Subtraction with int */
static inline fixed_t
subtract_int (fixed_t x, int n)
{
  return subtract (x, int_to_fixed_point (n));
}

/* Multiplication with int */
static inline fixed_t
multiply_int (fixed_t x, int n)
{
  return x * n;
}

/* Division with int */
static inline fixed_t
divide_int (fixed_t x, int n)
{
  return x / n;
}

#endif /* threads/fixed_point.h */