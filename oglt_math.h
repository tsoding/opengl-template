#ifndef OGLT_MATH_H_
#define OGLT_MATH_H_

static inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

#endif // OGLT_MATH_H_
