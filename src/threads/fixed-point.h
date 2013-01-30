#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define P 17
#define Q 14
#define F 1 << (Q)

#define MUL_INT(x, n) (x) * (n)
#define MUL_FP(x, y) ((int64_t)(x)) * (y) / (F)
#define DIV_INT(x, n) (x) / (n)
#define DIV_FP(x, y) ((int64_t)(x)) * (F) / (y)
#define ADD_INT(x, n) (x) + (n) * (F)
#define ADD_FP(x, y) (x) + (y) 

#define CONVERT_TO_INT_NEAREST(x) ((x) >= 0 ? ((x) + (F)/2)/(F) : ((x) - (F)/2)/(F))
#define CONVERT_TO_FP(n) (n) * (F)

#endif
