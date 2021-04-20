#ifndef OGLT_MATH_H_
#define OGLT_MATH_H_

#define V2_COUNT 2

typedef struct {
    float x, y;
} V2;

static inline V2 v2(float x, float y)
{
    return (V2) {.x = x, .y = y};
}

#define RGBA_COUNT 4

typedef struct {
    float r, g, b, a;
} RGBA;

static inline RGBA rgba(float r, float g, float b, float a)
{
    return (RGBA) {.r = r, .g = g, .b = b, .a = a};
}

static inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

#endif // OGLT_MATH_H_
