#ifndef __MEX_HELPERS_HPP__
#define __MEX_HELPERS_HPP__
#include <stdint.h>
#include <string>
#include <cstring>
#include <typeinfo>
#include <memory>
#include "matrix.h"

std::shared_ptr<std::string> to_string(const mxArray *ptr) {
    char *s = mxArrayToString(ptr);
    return std::make_shared<std::string>(s);
}

mxArray *from_string(const std::string str) {
    return mxCreateString(str.c_str());
}

int64_t to_int(const mxArray *ptr) {
    double *p = mxGetDoubles(ptr);
    return (int64_t) *p;
}

double to_double(const mxArray *ptr) {
    double *p = mxGetDoubles(ptr);
    return (double) *p;
}

mxArray *from_double(double val) {
#if MX_HAS_INTERLEAVED_COMPLEX    
    mxDouble  *dynamicData;        /* pointer to dynamic data */
    const mxDouble data[] = {val};  /* existing data */
    dynamicData = (mxDouble *) mxMalloc(1 * sizeof(double));
#else
    double  *dynamicData;          /* pointer to dynamic data */
    const double data[] = {val};  /* existing data */
    dynamicData = (double *) mxMalloc(1 * sizeof(double));
#endif

    dynamicData[0] = data[0];
    
    mxArray *ret = mxCreateNumericMatrix(0, 0, mxDOUBLE_CLASS, mxREAL);
#if MX_HAS_INTERLEAVED_COMPLEX
    mxSetDoubles(ret, dynamicData);
#else
    mxSetPr(ret, dynamicData);
#endif
    mxSetM(ret, 1);
    mxSetN(ret, 1);
    return ret;
}

mxArray *from_int32(int32_t val) {
#if MX_HAS_INTERLEAVED_COMPLEX    
    mxInt32  *dynamicData;        /* pointer to dynamic data */
    const mxInt32 data[] = {val};  /* existing data */
    dynamicData = (mxInt32 *) mxMalloc(1 * sizeof(int32_t));
#else
    int32_t  *dynamicData;          /* pointer to dynamic data */
    const int32_t data[] = {val};  /* existing data */
    dynamicData = (int32_t *) mxMalloc(1 * sizeof(int32_t));
#endif

    dynamicData[0] = data[0];
    
    mxArray *ret = mxCreateNumericMatrix(0, 0, mxINT32_CLASS, mxREAL);
#if MX_HAS_INTERLEAVED_COMPLEX
    mxSetInt32s(ret, dynamicData);
#else
    mxSetPr(ret, dynamicData);
#endif
    mxSetM(ret, 1);
    mxSetN(ret, 1);
    return ret;
}

mxArray *from_int(int64_t val) {
#if MX_HAS_INTERLEAVED_COMPLEX    
    mxInt64  *dynamicData;        /* pointer to dynamic data */
    const mxInt64 data[] = {val};  /* existing data */
    dynamicData = (mxInt64 *) mxMalloc(1 * sizeof(int64_t));
#else
    int64_t  *dynamicData;          /* pointer to dynamic data */
    const int64_t data[] = {val};  /* existing data */
    dynamicData = (int64_t *) mxMalloc(1 * sizeof(int64_t));
#endif

    dynamicData[0] = data[0];
    
    mxArray *ret = mxCreateNumericMatrix(0, 0, mxINT64_CLASS, mxREAL);
#if MX_HAS_INTERLEAVED_COMPLEX
    mxSetInt64s(ret, dynamicData);
#else
    mxSetPr(ret, dynamicData);
#endif
    mxSetM(ret, 1);
    mxSetN(ret, 1);
    return ret;
}

#endif
