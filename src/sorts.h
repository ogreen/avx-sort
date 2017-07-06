#ifndef HEADER_FILE_SORTS
#define HEADER_FILE_SORTS

#include "main.h"

/*
 * Merging Functions
 */
typedef void (*MergeTemplate)(vec_t*,uint32_t,vec_t*,uint32_t,vec_t*,uint32_t);

extern void serialMerge(
    vec_t* A, uint32_t A_length,
    vec_t* B, uint32_t B_length,
    vec_t* C, uint32_t C_length);

extern void serialMergeNoBranch(
    vec_t* A, uint32_t A_length,
    vec_t* B, uint32_t B_length,
    vec_t* C, uint32_t C_length);

extern void bitonicMergeReal(
    vec_t* A, uint32_t A_length,
    vec_t* B, uint32_t B_length,
    vec_t* C, uint32_t C_length);

#ifdef AVX512
extern void bitonicAVX512Merge(
    vec_t* A, uint32_t A_length,
    vec_t* B, uint32_t B_length,
    vec_t* C, uint32_t C_length);

extern void avx512Merge(
    vec_t* A, uint32_t A_length,
    vec_t* B, uint32_t B_length,
    vec_t* C, uint32_t C_length);
#endif

/*
 * Sorting Functions
 */
typedef void (*SortTemplate)(vec_t*, uint32_t, const uint32_t);

void quickSort(
    vec_t* array, uint32_t array_length, const uint32_t splitNumber);

template <MergeTemplate Merge>
void iterativeMergeSort(
    vec_t* array, uint32_t array_length, const uint32_t splitNumber);

typedef void (*ParallelSortTemplate)(vec_t*, uint32_t, const uint32_t);

template <SortTemplate Sort, MergeTemplate Merge>
void parallelIterativeMergeSort(
    vec_t* array, uint32_t array_length, const uint32_t splitNumber);

#endif
