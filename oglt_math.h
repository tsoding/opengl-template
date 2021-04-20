#ifndef OGLT_MATH_H_
#define OGLT_MATH_H_

#define V2_COUNT 2

typedef struct {
    float x, y;
} V2;

#define RGBA_COUNT 4

typedef struct {
    float r, g, b, a;
} RGBA;

static inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

#endif // OGLT_MATH_H_
