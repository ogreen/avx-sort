#ifndef HEADER_FILE_SORTS
#define HEADER_FILE_SORTS

#include "main.h"

struct memPointers {
    uint32_t* ASplitters;
    uint32_t* BSplitters;
    uint32_t* arraySizes;
};

/*
 * Merging Functions
 */

typedef void (*MergeTemplate)(vec_t*,uint32_t,vec_t*,uint32_t,vec_t*,uint32_t, struct memPointers*);
typedef void (*ParallelMergeTemplate)(vec_t*,uint32_t,vec_t*,uint32_t,vec_t*,uint32_t, struct memPointers*);
typedef void (*AlgoTemplate)(struct AlgoArgs*);

extern void serialMerge(struct AlgoArgs *args);
extern void serialMergeNoBranch(struct AlgoArgs *args);
extern void bitonicMergeReal(struct AlgoArgs *args);
extern void avx512Merge(struct AlgoArgs *args);

extern void avx512ParallelMerge(
    vec_t* A, uint32_t A_length,
    vec_t* B, uint32_t B_length,
    vec_t* C, uint32_t C_length,
    struct memPointers* pointers);

template <MergeTemplate Merge>
void parallelMerge(
    vec_t* A, uint32_t A_length,
    vec_t* B, uint32_t B_length,
    vec_t* C, uint32_t C_length,
    struct memPointers* pointers);

/*
 * Sorting Functions
 */
typedef void (*SortTemplate)(vec_t*, vec_t*, uint32_t, const uint32_t, struct memPointers*);

void quickSort(
    vec_t* array, vec_t* C, uint32_t array_length, const uint32_t splitNumber, struct memPointers* pointers);

void ippSort(
    vec_t* array, vec_t* C, uint32_t array_length, const uint32_t splitNumber, struct memPointers* pointers);


template <MergeTemplate Merge>
void avx512SortNoMergePath(
    vec_t* array, vec_t* C, uint32_t array_length, const uint32_t splitNumber, struct memPointers* pointers);

template <MergeTemplate Merge>
void avx512SortNoMergePathV2(
    vec_t* array, vec_t* C, uint32_t array_length, const uint32_t splitNumber, struct memPointers* pointers);

template <MergeTemplate Merge>
void iterativeMergeSort(
    vec_t* array, vec_t* C, uint32_t array_length, const uint32_t splitNumber, struct memPointers* pointers);

template <MergeTemplate Merge>
void iterativeMergeSortPower2(
    vec_t* array, vec_t* C, uint32_t array_length, const uint32_t splitNumber, struct memPointers* pointers);

typedef void (*ParallelSortTemplate)(vec_t*, vec_t*, uint32_t, const uint32_t, struct memPointers* pointers);

template <SortTemplate Sort, MergeTemplate Merge>
void parallelIterativeMergeSort(
    vec_t* array, vec_t* C, uint32_t array_length, const uint32_t splitNumber, struct memPointers* pointers);

template <SortTemplate Sort, MergeTemplate Merge>
void parallelIterativeMergeSortPower2(
    vec_t* array, vec_t* C, uint32_t array_length, const uint32_t splitNumber, struct memPointers* pointers);

template <SortTemplate Sort, MergeTemplate Merge>
void parallelIterativeMergeSortV2(
    vec_t* array, vec_t* C, uint32_t array_length, const uint32_t splitNumber, struct memPointers* pointers);

#endif
