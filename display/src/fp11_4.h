#ifndef _FP11_4_H
#define _FP11_4_H

#define FP11_4_ZERO_POINT_FIVE (0x8)

typedef int_fast16_t fp11_4_t;
struct point11_4_t {
    fp11_4_t x, y;
};

static inline fp11_4_t fp11_4_add(const fp11_4_t a, const fp11_4_t b)
{
    return a + b;
}

static inline fp11_4_t fp11_4_sub(const fp11_4_t a, const fp11_4_t b)
{
    return a - b;
}

static inline fp11_4_t fp11_4_mul(const fp11_4_t a, const fp11_4_t b)
{
    int_fast32_t aim = a, bim = b;
    return (aim * bim) >> 4;
}

static inline fp11_4_t fp11_4_div(const fp11_4_t a, const fp11_4_t b)
{
    return ((a << 4) / b);
}

static inline fp11_4_t fp11_4_from_int16_t(const int16_t v)
{
    return (v << 4) & 0xfff0;
}

static inline int16_t fp11_4_trunc(const fp11_4_t v)
{
    return v >> 4;
}

static inline fp11_4_t fp11_4_avg(const fp11_4_t a, const fp11_4_t b)
{
    return (a >> 1) + (b >> 1);
}

#ifdef FPMATH_WITH_FLOATS

static inline fp11_4_t fp11_4_from_float(const float v)
{
    return v * (1<<4);
}

static inline float fp11_4_to_float(const fp11_4_t v)
{
    return v / (float)(1<<4);
}

#endif

#endif
