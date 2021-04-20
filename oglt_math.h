#ifndef OGLT_MATH_H_
#define OGLT_MATH_H_

#define V2_COUNT 2

typedef struct {
    float x, y;
} V2;

static inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

#endif // OGLT_MATH_H_
