/*
  Authors: Alex Watkins - https://github.com/jalexw1013 - http://alexwatkins.co
           Oded Green - https://github.com/ogreen
           Other minor authors are noted next to their contributions in the code

         Copyright (c) 2018 Alex Watkins, All rights reserved.

         Redistribution and use in source and binary forms, with or without
         modification, are permitted provided that the following conditions
         are met:

         1. Redistributions of source code must retain the above copyright
         notice, this list of conditions and the following disclaimer.

         2. Redistributions in binary form must reproduce the above copyright
         notice, this list of conditions and the following disclaimer in the
         documentation and/or other materials provided with the distribution.

         3. Neither the name of the copyright holder nor the names of its
         contributors may be used to endorse or promote products derived
         from this software without specific prior written permission.

         THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
         "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
         LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
         FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
         COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
         INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
         BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
         LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
         CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
         LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
         ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
         POSSIBILITY OF SUCH DAMAGE.
*/
#include <stdio.h>
#include <stdint.h>
#include <float.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <omp.h>
#include <malloc.h>
#include <x86intrin.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <immintrin.h>
#include <sys/time.h>
//#include <../tools/intel64/staticlib/ipp_n0.h>
#include <ipp.h>

#include "utils/util.h"
#include "utils/xmalloc.h"
#include "sorts.h"
//#include "ipp.h"
//#include "ipp.h"
//#include <tbb/tbb.h>
//#include "tbb/parallel_sort.h"

////////////////////////////////////////////////////////////////////////////////
//
// Merging algorithms
//
////////////////////////////////////////////////////////////////////////////////

inline void serialMerge(struct AlgoArgs *args) {
    vec_t* A = args->A;
    uint32_t A_length = args->A_length;
    vec_t* B = args->B;
    uint32_t B_length = args->B_length;
    vec_t* C = args->C;
    uint32_t Aindex = 0;
    uint32_t Bindex = 0;
    uint32_t Cindex = 0;

    while(Aindex < A_length && Bindex < B_length) {
        C[Cindex++] = A[Aindex] < B[Bindex] ? A[Aindex++] : B[Bindex++];
    }
    while(Aindex < A_length) C[Cindex++] = A[Aindex++];
    while(Bindex < B_length) C[Cindex++] = B[Bindex++];
}

inline void serialMergeNoBranch(struct AlgoArgs *args) {
    vec_t* A = args->A;
    uint32_t A_length = args->A_length;
    vec_t* B = args->B;
    uint32_t B_length = args->B_length;
    vec_t* C = args->C;
    uint32_t Aindex = 0;
    uint32_t Bindex = 0;
    uint32_t Cindex = 0;
    int32_t flag;

    while(Aindex < A_length && Bindex < B_length) {
        flag = ((unsigned int)(A[Aindex] - B[Bindex]) >> 31 ) ;
        C[Cindex++] = (flag)*A[Aindex] + (1-flag)*B[Bindex];
        Aindex +=flag;
        Bindex +=1-flag;
    }
    while(Aindex < A_length) C[Cindex++] = A[Aindex++];
    while(Bindex < B_length) C[Cindex++] = B[Bindex++];
}

/*
 * SSE Merge Sort From Srinivas's code
 * https://github.com/psombe/sorting
 */
const uint8_t m0110 =          (1<<4) | (1<<2);
const uint8_t m1010 = (1<<6) |          (1<<2);
const uint8_t m1100 = (1<<6) | (1<<4);
const uint8_t m1221 = (1<<6) | (2<<4) | (2<<2) | 1;
const uint8_t m2121 = (2<<6) | (1<<4) | (2<<2) | 1;
const uint8_t m2332 = (2<<6) | (3<<4) | (3<<2) | 2;
const uint8_t m3120 = (3<<6) | (1<<4) | (2<<2) | 0;
const uint8_t m3232 = (3<<6) | (2<<4) | (3<<2) | 2;

const uint8_t m0123 = (0<<6) | (1<<4) | (2<<2) | 3;
const uint8_t m0321 = (0<<6) | (3<<4) | (2<<2) | 1;
const uint8_t m2103 = (2<<6) | (1<<4) | (0<<2) | 3;
const uint8_t m0213 = (0<<6) | (2<<4) | (1<<2) | 3;
const uint8_t m1001 = (1<<6)                   | 1;

inline void bitonicMergeReal(struct AlgoArgs *args) {
    vec_t* A = args->A;
    uint32_t A_length = args->A_length;
    vec_t* B = args->B;
    uint32_t B_length = args->B_length;
    vec_t* C = args->C;
    uint32_t C_length = args->C_length;

    // TODO I think these can be 4s
    if (A_length < 5 || B_length < 5 || C_length < 5) {
        serialMerge(args);
        return;
    }

    long Aindex = 0,Bindex = 0, Cindex = 0;
    int isA = 0;//, isB;

    __m128i sA = _mm_loadu_si128((const __m128i*)&(A[Aindex]));
    __m128i sB = _mm_loadu_si128((const __m128i*)&(B[Bindex]));
    while ((Aindex < (A_length-4)) && (Bindex < (B_length-4)))
    {
        // load SIMD registers from A and B
        isA = 0;
        //isB = 0;
        // reverse B
        sB = _mm_shuffle_epi32(sB, m0123);
        // level 1
        __m128i sL1 = _mm_min_epu32(sA, sB);
        __m128i sH1 = _mm_max_epu32(sA, sB);
        __m128i sL1p = _mm_unpackhi_epi64(sH1, sL1);
        __m128i sH1p = _mm_unpacklo_epi64(sH1, sL1);
        // level 2
        __m128i sL2 = _mm_min_epu32(sH1p, sL1p);
        __m128i sH2 = _mm_max_epu32(sH1p, sL1p);
        __m128i c1010 = _mm_set_epi32(-1, 0, -1, 0);
        __m128i c0101 = _mm_set_epi32(0, -1, 0, -1);
        // use blend
        __m128i sL2p = _mm_or_si128(_mm_and_si128(sL2, c1010), _mm_and_si128(_mm_shuffle_epi32(sH2, m0321), c0101));
        __m128i sH2p = _mm_or_si128(_mm_and_si128(_mm_shuffle_epi32(sL2, m2103), c1010), _mm_and_si128(sH2, c0101));
        // level 3
        __m128i sL3 = _mm_min_epu32(sL2p, sH2p);
        __m128i sH3 = _mm_max_epu32(sL2p, sH2p);
        __m128i sL3p = _mm_shuffle_epi32(_mm_unpackhi_epi64(sH3, sL3), m0213);
        __m128i sH3p = _mm_shuffle_epi32(_mm_unpacklo_epi64(sH3, sL3), m0213);
        // store back data into C from SIMD registers
        _mm_storeu_si128((__m128i*)&(C[Cindex]), sL3p);
        // calculate index for the next run
        sB=sH3p;
        Cindex+=4;
        if (A[Aindex+4]<B[Bindex+4]){
            Aindex+=4;
            isA = 1;
            sA = _mm_loadu_si128((const __m128i*)&(A[Aindex]));
        }
        else {
            Bindex+=4;
            //isB = 1;
            sA = _mm_loadu_si128((const __m128i*)&(B[Bindex]));
        }
    }
    if( isA ) Bindex += 4;
    else Aindex += 4;

    //int tempindex = 0;
    //int temp_length = 4;
    vec_t temp[4];
    _mm_storeu_si128((__m128i*)temp, sB);

    if (temp[3] <= A[Aindex])
    {
        Aindex -= 4;
        _mm_storeu_si128((__m128i*)&(A[Aindex]), sB);
    }
    else
    {
        Bindex -= 4;
        _mm_storeu_si128((__m128i*)&(B[Bindex]), sB);
    }
    while (Cindex < C_length)
    {
        if (Aindex < A_length && Bindex < B_length)
        {
            if (A[Aindex] < B[Bindex])
            {
                C[Cindex++] = A[Aindex++];
            }
            else
            {
                C[Cindex++] = B[Bindex++];
            }
        }
        else
        {
            while (Aindex < A_length)
            {
                C[Cindex++] = A[Aindex++];
            }
            while (Bindex < B_length)
            {
                C[Cindex++] = B[Bindex++];
            }
        }
    }
    return;
}

inline void avx512Merge(struct AlgoArgs *args) {
    vec_t* A = args->A;
    uint32_t A_length = args->A_length;
    vec_t* B = args->B;
    uint32_t B_length = args->B_length;
    vec_t* C = args->C;
    uint32_t C_length = args->C_length;
    uint32_t ASplitters[17];
    uint32_t BSplitters[17];

    MergePathSplitter(A, A_length, B, B_length, C,
        C_length, 16, ASplitters, BSplitters);

    //start indexes
    __m512i vindexA = _mm512_load_epi32(ASplitters);
    __m512i vindexB = _mm512_load_epi32(BSplitters);
    __m512i vindexC = _mm512_add_epi32(vindexA, vindexB);
    //stop indexes
    const __m512i vindexAStop = _mm512_load_epi32(ASplitters + 1);
    const __m512i vindexBStop = _mm512_load_epi32(BSplitters + 1);
    //other Variables
    static const __m512i mizero = _mm512_set_epi32(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
    static const __m512i mione = _mm512_set_epi32(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1);
    __mmask16 exceededAStop = _mm512_cmpgt_epi32_mask(vindexAStop, vindexA);
    __mmask16 exceededBStop = _mm512_cmpgt_epi32_mask(vindexBStop, vindexB);
    while ((exceededAStop | exceededBStop) != 0) {
       //get the current elements
        __m512i miAelems = _mm512_mask_i32gather_epi32(mizero, exceededAStop, vindexA, (const int *)A, 4);
        __m512i miBelems = _mm512_mask_i32gather_epi32(mizero, exceededBStop, vindexB, (const int *)B, 4);
        //compare the elements
        __mmask16 micmp = _mm512_cmple_epi32_mask(miAelems, miBelems);
        micmp = (~exceededBStop | (micmp & exceededAStop));
        //copy the elements to the final elements
        __m512i miCelems = _mm512_mask_blend_epi32(micmp, miBelems, miAelems);
        _mm512_mask_i32scatter_epi32((int *)C, exceededAStop | exceededBStop, vindexC, miCelems, 4);
        //increase indexes
        vindexA = _mm512_mask_add_epi32(vindexA, exceededAStop & micmp, vindexA, mione);
        vindexB = _mm512_mask_add_epi32(vindexB, exceededBStop & ~micmp, vindexB, mione);
        exceededAStop = _mm512_cmpgt_epi32_mask(vindexAStop, vindexA);
        exceededBStop = _mm512_cmpgt_epi32_mask(vindexBStop, vindexB);
        vindexC = _mm512_mask_add_epi32(vindexC, exceededAStop | exceededBStop, vindexC, mione);
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Parallel Merging algorithms
//
////////////////////////////////////////////////////////////////////////////////

template <AlgoTemplate Merge>
void parallelMerge(struct AlgoArgs *args)
{
    vec_t* A = args->A;
    uint32_t A_length = args->A_length;
    vec_t* B = args->B;
    uint32_t B_length = args->B_length;
    vec_t* C = args->C;
    uint32_t C_length = args->C_length;
    vec_t* array = args->CUnsorted;
    uint32_t array_length = args->C_length;

    // #pragma omp parallel
    // {
    #pragma omp parallel for
    for(int t=0; t< omp_get_num_threads(); t++)
    {
        uint32_t numThreads = omp_get_num_threads();
        uint32_t threadNum = t;//omp_get_thread_num();
        uint32_t* ASplitters = args->ASplitters + (numThreads + 1) * threadNum;
        uint32_t* BSplitters = args->BSplitters + (numThreads + 1) * threadNum;
        MergePathSplitter(A, A_length, B, B_length, C,
            C_length, numThreads, ASplitters, BSplitters);
        uint32_t A_split_length = ASplitters[threadNum + 1] - ASplitters[threadNum];
        uint32_t B_split_length = BSplitters[threadNum + 1] - BSplitters[threadNum];

        struct AlgoArgs mergeArgs;
        mergeArgs.A = A + ASplitters[threadNum];
        mergeArgs.A_length = A_split_length;
        mergeArgs.B = B + BSplitters[threadNum];
        mergeArgs.B_length = B_split_length;
        mergeArgs.C = C + ASplitters[threadNum] + BSplitters[threadNum];
        mergeArgs.C_length = A_split_length + B_split_length;
        mergeArgs.threadNum = threadNum;

        Merge(&mergeArgs);
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Sorting algorithms
//
////////////////////////////////////////////////////////////////////////////////

void quickSort(struct AlgoArgs *args) {
    qsort((void*)args->CUnsorted, args->C_length, sizeof(vec_t), hostBasicCompare);
}

void ippSort(struct AlgoArgs *args) {
    ippsSortAscend_32s_I((Ipp32s*)args->CUnsorted, args->C_length);
}

void ippRadixSort(struct AlgoArgs *args) {
    ippsSortRadixAscend_32u_I((Ipp32u*)args->CUnsorted, args->C_length, (Ipp8u*)args->C);
}

void tbbSort(struct AlgoArgs *args) {
    //tbb::parallel_sort(args->CUnsorted, args->CUnsorted + args->C_length);
}


/*
This next algorithm:
Copyright (c) 2014, Haichuan Wang
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define BASE_BITS 8
#define BASE (1 << BASE_BITS)
#define MASK (BASE-1)
#define DIGITS(v, shift) (((v) >> shift) & MASK)

void omp_lsd_radix_sort(size_t n, uint32_t data[n], uint32_t* buffer) {
    int total_digits = sizeof(uint32_t)*8;

    //Each thread use local_bucket to move data
    size_t i;
    for(int shift = 0; shift < total_digits; shift+=BASE_BITS) {
        size_t bucket[BASE] = {0};

        size_t local_bucket[BASE] = {0}; // size needed in each bucket/thread
        //1st pass, scan whole and check the count
        #pragma omp parallel firstprivate(local_bucket)
        {
            #pragma omp for schedule(static) nowait
            for(i = 0; i < n; i++){
                local_bucket[DIGITS(data[i], shift)]++;
            }
            #pragma omp critical
            for(i = 0; i < BASE; i++) {
                bucket[i] += local_bucket[i];
            }
            #pragma omp barrier
            #pragma omp single
            for (i = 1; i < BASE; i++) {
                bucket[i] += bucket[i - 1];
            }
            int nthreads = omp_get_num_threads();
            int tid = omp_get_thread_num();
            for(int cur_t = nthreads - 1; cur_t >= 0; cur_t--) {
                if(cur_t == tid) {
                    for(i = 0; i < BASE; i++) {
                        bucket[i] -= local_bucket[i];
                        local_bucket[i] = bucket[i];
                    }
                } else { //just do barrier
                    #pragma omp barrier
                }

            }
            #pragma omp for schedule(static)
            for(i = 0; i < n; i++) { //note here the end condition
                buffer[local_bucket[DIGITS(data[i], shift)]++] = data[i];
            }
        }
        //now move data
        uint32_t* tmp = data;
        data = buffer;
        buffer = tmp;
    }
}

void haichuanwangSort(struct AlgoArgs *args) {
    omp_lsd_radix_sort((size_t)args->C_length, args->CUnsorted, args->C);
}

template <AlgoTemplate Merge>
void avx512SortNoMergePathV2(struct AlgoArgs *args) {
    vec_t* C = args->C;
    uint32_t C_length = args->C_length;
    vec_t* array = args->CUnsorted;
    uint32_t array_length = args->C_length;

    // Round one, take unsorted array into sub arrays of size 2
    for (uint32_t index = 0; index < array_length; index += 32) {
        // Get Elements
        __m512i miAelems = _mm512_load_epi32(array + index);
        __m512i miBelems = _mm512_load_epi32(array + index + 16);

        //compare the elements
        __mmask16 micmp = _mm512_cmple_epi32_mask(miAelems, miBelems);

        //copy the elements to the final elements
        __m512i miC1elems = _mm512_mask_blend_epi32(micmp, miBelems, miAelems);
        __m512i miC2elems = _mm512_mask_blend_epi32(micmp, miAelems, miBelems);

        _mm512_store_epi32((int *)array + index, miC1elems);
        _mm512_store_epi32((int *)array + index + 16, miC2elems);
    }

    __m512i roundTwoMax = _mm512_set_epi32(2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2);
    __m512i CindexOriginal = _mm512_set_epi32(60, 56, 52, 48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 4, 0);
    static const __m512i mione = _mm512_set_epi32(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1);
    static const __m512i miZero = _mm512_set_epi32(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);

    for (uint32_t index = 0; index < array_length; index += 64) {
        __m512i Cindex = CindexOriginal;
        __m512i ACount = miZero;
        __m512i BCount = miZero;
        __m512i miA0elems = _mm512_load_epi32(array + index);
        __m512i miA1elems = _mm512_load_epi32(array + index + 16);
        __m512i miB0elems = _mm512_load_epi32(array + index + 32);
        __m512i miB1elems = _mm512_load_epi32(array + index + 48);
        __mmask16 micmp = _mm512_cmple_epi32_mask(miA0elems, miB0elems);
        __m512i miC0elems = _mm512_mask_blend_epi32(micmp, miB0elems, miA0elems);
        _mm512_i32scatter_epi32((int *)array + index, Cindex, miC0elems, 4);
        Cindex = _mm512_add_epi32(Cindex, mione);
        ACount = _mm512_mask_add_epi32(ACount, micmp, ACount, mione);
        BCount = _mm512_mask_add_epi32(BCount, ~micmp, BCount, mione);
        miA0elems = _mm512_mask_blend_epi32(micmp, miA0elems, miA1elems);
        miB0elems = _mm512_mask_blend_epi32(micmp, miB1elems, miB0elems);
        micmp = _mm512_cmple_epi32_mask(miA0elems, miB0elems);
        __m512i miC1elems = _mm512_mask_blend_epi32(micmp, miB0elems, miA0elems);
        _mm512_i32scatter_epi32((int *)array + index, Cindex, miC1elems, 4);
        Cindex = _mm512_add_epi32(Cindex, mione);
        ACount = _mm512_mask_add_epi32(ACount, micmp, ACount, mione);
        BCount = _mm512_mask_add_epi32(BCount, ~micmp, BCount, mione);
        miA0elems = _mm512_mask_blend_epi32(micmp, miA0elems, miA1elems);
        miB0elems = _mm512_mask_blend_epi32(micmp, miB1elems, miB0elems);
        micmp = _mm512_cmple_epi32_mask(miA0elems, miB0elems);
        micmp &= _mm512_cmplt_epi32_mask(ACount, roundTwoMax);
        micmp |= _mm512_cmpge_epi32_mask(BCount, roundTwoMax);
        __m512i miC2elems = _mm512_mask_blend_epi32(micmp, miB0elems, miA0elems);
        _mm512_i32scatter_epi32((int *)array + index, Cindex, miC2elems, 4);
        Cindex = _mm512_add_epi32(Cindex, mione);
        ACount = _mm512_mask_add_epi32(ACount, micmp, ACount, mione);
        BCount = _mm512_mask_add_epi32(BCount, ~micmp, BCount, mione);
        miA0elems = _mm512_mask_blend_epi32(micmp, miA0elems, miA1elems);
        miB0elems = _mm512_mask_blend_epi32(micmp, miB1elems, miB0elems);
        micmp = _mm512_cmple_epi32_mask(miA0elems, miB0elems);
        micmp &= _mm512_cmplt_epi32_mask(ACount, roundTwoMax);
        micmp |= _mm512_cmpge_epi32_mask(BCount, roundTwoMax);
        __m512i miC3elems = _mm512_mask_blend_epi32(micmp, miB0elems, miA0elems);
        _mm512_i32scatter_epi32((int *)array + index, Cindex, miC3elems, 4);
    }

    __m512i vindexA = _mm512_set_epi32(60, 56, 52, 48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 4, 0);
    __m512i vindexB = _mm512_set_epi32(62, 58, 54, 50, 46, 42, 38, 34, 30, 26, 22, 18, 14, 10, 6, 2);
    __m512i vindexBStop = _mm512_set_epi32(64, 60, 56, 52, 48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 4);

    __m512i vindexAInner, vindexBInner, vindexCInner;
    __m512i miAelems, miBelems, miCelems;
    __mmask16 maskA = (__mmask16)0xFFFF, maskB = (__mmask16)0xFFFF, micmp;
    uint32_t numberOfSwaps = 0;

    uint32_t sortedArraySize = 4;
    for (; sortedArraySize < array_length / 32; sortedArraySize <<= 1) {
        vindexA = _mm512_slli_epi32(vindexA, 1);
        vindexB = _mm512_slli_epi32(vindexB, 1);
        vindexBStop = _mm512_slli_epi32(vindexBStop, 1);
        for (uint32_t index = 0; index < array_length; index += 32 * sortedArraySize) {
            vindexAInner = vindexA;
            vindexBInner = vindexB;
            vindexCInner = vindexA;
            miAelems = _mm512_i32gather_epi32(vindexAInner, (const int *)array + index, 4);
            miBelems = _mm512_i32gather_epi32(vindexBInner, (const int *)array + index, 4);

            //compare the elements
            micmp = _mm512_cmple_epi32_mask(miAelems, miBelems);
            //copy the elements to the final elements
            miCelems = _mm512_mask_blend_epi32(micmp, miBelems, miAelems);
            _mm512_i32scatter_epi32((int *)C + index, vindexCInner, miCelems, 4);

            // increase indexes
            vindexAInner = _mm512_mask_add_epi32(vindexAInner, micmp, vindexAInner, mione);
            vindexBInner = _mm512_mask_add_epi32(vindexBInner, ~micmp, vindexBInner, mione);
            vindexCInner = _mm512_add_epi32(vindexCInner, mione);
            uint32_t l1Index = index + 16;
            for (; l1Index < index + 32 * sortedArraySize - 16; l1Index += 16) {
                maskA = _mm512_cmplt_epi32_mask(vindexAInner, vindexB);
                maskB = _mm512_cmplt_epi32_mask(vindexBInner, vindexBStop);
                miAelems = _mm512_mask_i32gather_epi32(miAelems, micmp & maskA, vindexAInner, (const int *)array + index, 4);
                miBelems = _mm512_mask_i32gather_epi32(miBelems, (~micmp) & maskB, vindexBInner, (const int *)array + index, 4);

                micmp = _mm512_mask_cmple_epi32_mask(maskA, miAelems, miBelems);
                micmp |= ~maskB;
                //copy the elements to the final elements
                miCelems = _mm512_mask_blend_epi32(micmp, miBelems, miAelems);
                _mm512_i32scatter_epi32((int *)C + index, vindexCInner, miCelems, 4);

                // increase indexes
                vindexAInner = _mm512_mask_add_epi32(vindexAInner, micmp, vindexAInner, mione);
                vindexBInner = _mm512_mask_add_epi32(vindexBInner, ~micmp, vindexBInner, mione);
                vindexCInner = _mm512_add_epi32(vindexCInner, mione);
            }
            maskA = _mm512_cmplt_epi32_mask(vindexAInner, vindexB);
            maskB = _mm512_cmplt_epi32_mask(vindexBInner, vindexBStop);

            miAelems = _mm512_mask_i32gather_epi32(miAelems, micmp & maskA, vindexAInner, (const int *)array + index, 4);
            miBelems = _mm512_mask_i32gather_epi32(miBelems, (~micmp) & maskB, vindexBInner, (const int *)array + index, 4);

            //compare the elements
            micmp = _mm512_mask_cmple_epi32_mask(maskA, miAelems, miBelems);
            micmp |= ~maskB;
            miCelems = _mm512_mask_blend_epi32(micmp, miBelems, miAelems);
            _mm512_i32scatter_epi32((int *)C + index, vindexCInner, miCelems, 4);
        }
        // Pointer Swap
        vec_t* tmp = array;
        array = C;
        C = tmp;
        numberOfSwaps++;
    }

    for (; sortedArraySize < array_length; sortedArraySize <<= 1) {
        for (uint32_t A_start = 0; A_start < array_length; A_start += 2 * sortedArraySize)
    	{
            uint32_t A_end = min(A_start + sortedArraySize, array_length - 1);
    		uint32_t B_start = A_end;
    		uint32_t B_end = min(A_start + 2 * sortedArraySize, array_length);
            uint32_t A_length = A_end - A_start;
            uint32_t B_length = B_end - B_start;

            struct AlgoArgs mergeArgs;
            mergeArgs.A = array + A_start;
            mergeArgs.A_length = A_length;
            mergeArgs.B = array + B_start;
            mergeArgs.B_length = B_length;
            mergeArgs.C = C + A_start;
            mergeArgs.C_length = A_length + B_length;

            Merge(&mergeArgs);
    	}
        //pointer swap for C
        vec_t* tmp = array;
        array = C;
        C = tmp;
        numberOfSwaps++;
    }

    if (numberOfSwaps%2 == 1) {
        memcpy((void*)C,(void*)array, (array_length)*sizeof(vec_t));
        vec_t* tmp = array;
        array = C;
        C = tmp;
    }
}

template <AlgoTemplate Merge>
void avx512SortNoMergePathV3(struct AlgoArgs *args) {
    vec_t* C = args->C;
    uint32_t C_length = args->C_length;
    vec_t* array = args->CUnsorted;
    uint32_t array_length = args->C_length;

    // Round one, take unsorted array into sub arrays of size 2
    uint32_t numberOfExtraElements = array_length%64;
    uint32_t max64Value = array_length - numberOfExtraElements;
    uint32_t index;
    for (index = 0; index < max64Value; index += 32) {
        // Get Elements
        __m512i miAelems = _mm512_load_epi32(array + index);
        __m512i miBelems = _mm512_load_epi32(array + index + 16);

        //compare the elements
        __mmask16 micmp = _mm512_cmple_epi32_mask(miAelems, miBelems);

        //copy the elements to the final elements
        __m512i miC1elems = _mm512_mask_blend_epi32(micmp, miBelems, miAelems);
        __m512i miC2elems = _mm512_mask_blend_epi32(micmp, miAelems, miBelems);

        _mm512_store_epi32((int *)array + index, miC1elems);
        _mm512_store_epi32((int *)array + index + 16, miC2elems);
    }

    __m512i roundTwoMax = _mm512_set_epi32(2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2);
    __m512i CindexOriginal = _mm512_set_epi32(60, 56, 52, 48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 4, 0);
    static const __m512i mione = _mm512_set_epi32(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1);
    static const __m512i miZero = _mm512_set_epi32(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);

    for (uint32_t index = 0; index < max64Value; index += 64) {
        __m512i Cindex = CindexOriginal;
        __m512i ACount = miZero;
        __m512i BCount = miZero;
        __m512i miA0elems = _mm512_load_epi32(array + index);
        __m512i miA1elems = _mm512_load_epi32(array + index + 16);
        __m512i miB0elems = _mm512_load_epi32(array + index + 32);
        __m512i miB1elems = _mm512_load_epi32(array + index + 48);
        __mmask16 micmp = _mm512_cmple_epi32_mask(miA0elems, miB0elems);
        __m512i miC0elems = _mm512_mask_blend_epi32(micmp, miB0elems, miA0elems);
        _mm512_i32scatter_epi32((int *)array + index, Cindex, miC0elems, 4);
        Cindex = _mm512_add_epi32(Cindex, mione);
        ACount = _mm512_mask_add_epi32(ACount, micmp, ACount, mione);
        BCount = _mm512_mask_add_epi32(BCount, ~micmp, BCount, mione);
        miA0elems = _mm512_mask_blend_epi32(micmp, miA0elems, miA1elems);
        miB0elems = _mm512_mask_blend_epi32(micmp, miB1elems, miB0elems);
        micmp = _mm512_cmple_epi32_mask(miA0elems, miB0elems);
        __m512i miC1elems = _mm512_mask_blend_epi32(micmp, miB0elems, miA0elems);
        _mm512_i32scatter_epi32((int *)array + index, Cindex, miC1elems, 4);
        Cindex = _mm512_add_epi32(Cindex, mione);
        ACount = _mm512_mask_add_epi32(ACount, micmp, ACount, mione);
        BCount = _mm512_mask_add_epi32(BCount, ~micmp, BCount, mione);
        miA0elems = _mm512_mask_blend_epi32(micmp, miA0elems, miA1elems);
        miB0elems = _mm512_mask_blend_epi32(micmp, miB1elems, miB0elems);
        micmp = _mm512_cmple_epi32_mask(miA0elems, miB0elems);
        micmp &= _mm512_cmplt_epi32_mask(ACount, roundTwoMax);
        micmp |= _mm512_cmpge_epi32_mask(BCount, roundTwoMax);
        __m512i miC2elems = _mm512_mask_blend_epi32(micmp, miB0elems, miA0elems);
        _mm512_i32scatter_epi32((int *)array + index, Cindex, miC2elems, 4);
        Cindex = _mm512_add_epi32(Cindex, mione);
        ACount = _mm512_mask_add_epi32(ACount, micmp, ACount, mione);
        BCount = _mm512_mask_add_epi32(BCount, ~micmp, BCount, mione);
        miA0elems = _mm512_mask_blend_epi32(micmp, miA0elems, miA1elems);
        miB0elems = _mm512_mask_blend_epi32(micmp, miB1elems, miB0elems);
        micmp = _mm512_cmple_epi32_mask(miA0elems, miB0elems);
        micmp &= _mm512_cmplt_epi32_mask(ACount, roundTwoMax);
        micmp |= _mm512_cmpge_epi32_mask(BCount, roundTwoMax);
        __m512i miC3elems = _mm512_mask_blend_epi32(micmp, miB0elems, miA0elems);
        _mm512_i32scatter_epi32((int *)array + index, Cindex, miC3elems, 4);
    }

    __m512i vindexA512 = _mm512_set_epi32(60, 56, 52, 48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 4, 0);
    __m512i vindexB512 = _mm512_set_epi32(62, 58, 54, 50, 46, 42, 38, 34, 30, 26, 22, 18, 14, 10, 6, 2);
    __m512i vindexBStop512 = _mm512_set_epi32(64, 60, 56, 52, 48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 4);
    __m256i vindexA256 = _mm256_set_epi32(28, 24, 20, 16, 12, 8, 4, 0);
    __m256i vindexB256 = _mm256_set_epi32(30, 26, 22, 18, 14, 10, 6, 2);
    __m256i vindexBStop256 = _mm256_set_epi32(32, 28, 24, 20, 16, 12, 8, 4);
    __m128i vindexA128 = _mm128_set_epi32(12, 8, 4, 0);
    __m128i vindexB128 = _mm128_set_epi32(14, 10, 6, 2);
    __m128i vindexBStop128 = _mm128_set_epi32(16, 12, 8, 4);

    __m512i vindexAInner512, vindexBInner512, vindexCInner512;
    __m256i vindexAInner256, vindexBInner256, vindexCInner256;
    __m128i vindexAInner128, vindexBInner128, vindexCInner128;
    __m512i miAelems512, miBelems512, miCelems512;
    __m512i miAelems256, miBelems256, miCelems256;
    __m512i miAelems128, miBelems128, miCelems128;
    __mmask16 maskA512 = (__mmask16)0xFFFF, maskB512 = (__mmask16)0xFFFF, micmp512;
    __mmask16 maskA256 = (__mmask16)0xFFFF, maskB256 = (__mmask16)0xFFFF, micmp256;
    __mmask16 maskA128 = (__mmask16)0xFFFF, maskB128 = (__mmask16)0xFFFF, micmp128;
    uint32_t numberOfSwaps = 0;

    uint32_t sortedArraySize = 4;
    for (; sortedArraySize < array_length / 32; sortedArraySize <<= 1) {
        vindexA512 = _mm512_slli_epi32(vindexA512, 1);
        vindexB512 = _mm512_slli_epi32(vindexB512, 1);
        vindexBStop512 = _mm512_slli_epi32(vindexBStop512, 1);
        vindexA256 = _mm512_slli_epi32(vindexA256, 1);
        vindexB256 = _mm512_slli_epi32(vindexB256, 1);
        vindexBStop256 = _mm512_slli_epi32(vindexBStop256, 1);
        vindexA128 = _mm512_slli_epi32(vindexA128, 1);
        vindexB128 = _mm512_slli_epi32(vindexB128, 1);
        vindexBStop128 = _mm512_slli_epi32(vindexBStop128, 1);

        // Merge Using 512 bit vectors
        uint32_t numberOfExtraElementsMain = array_length%(32 * sortedArraySize);
        uint32_t maxValueMain = array_length - numberOfExtraElementsMain;
        uint32_t index;
        for (index = 0; index < maxValueMain; index += 32 * sortedArraySize) {
            vindexAInner512 = vindexA512;
            vindexBInner512 = vindexB512;
            vindexCInner512 = vindexA512;
            miAelems512 = _mm512_i32gather_epi32(vindexAInner512, (const int *)array + index, 4);
            miBelems512 = _mm512_i32gather_epi32(vindexBInner512, (const int *)array + index, 4);

            //compare the elements
            micmp512 = _mm512_cmple_epi32_mask(miAelems512, miBelems512);
            //copy the elements to the final elements
            miCelems512 = _mm512_mask_blend_epi32(micmp512, miBelems512, miAelems512);
            _mm512_i32scatter_epi32((int *)C + index, vindexCInner512, miCelems512, 4);

            // increase indexes
            vindexAInner512 = _mm512_mask_add_epi32(vindexAInner512, micmp512, vindexAInner512, mione512);
            vindexBInner512 = _mm512_mask_add_epi32(vindexBInner512, ~micmp512, vindexBInner512, mione512);
            vindexCInner512 = _mm512_add_epi32(vindexCInner512, mione);
            uint32_t l1Index = index + 16;
            for (; l1Index < index + 32 * sortedArraySize - 16; l1Index += 16) {
                maskA512 = _mm512_cmplt_epi32_mask(vindexAInner512, vindexB512);
                maskB512 = _mm512_cmplt_epi32_mask(vindexBInner512, vindexBStop512);
                miAelems512 = _mm512_mask_i32gather_epi32(miAelems512, micmp512 & maskA512, vindexAInner512, (const int *)array + index, 4);
                miBelems512 = _mm512_mask_i32gather_epi32(miBelems512, (~micmp512) & maskB512, vindexBInner512, (const int *)array + index, 4);

                micmp512 = _mm512_mask_cmple_epi32_mask(maskA512, miAelems512, miBelems512);
                micmp512 |= ~maskB512;
                //copy the elements to the final elements
                miCelems512 = _mm512_mask_blend_epi32(micmp512, miBelems512, miAelems512);
                _mm512_i32scatter_epi32((int *)C + index, vindexCInner512, miCelems512, 4);

                // increase indexes
                vindexAInner512 = _mm512_mask_add_epi32(vindexAInner512, micmp512, vindexAInner512, mione512);
                vindexBInner512 = _mm512_mask_add_epi32(vindexBInner512, ~micmp512, vindexBInner512, mione512);
                vindexCInner512 = _mm512_add_epi32(vindexCInner512, mione512);
            }
            maskA512 = _mm512_cmplt_epi32_mask(vindexAInner512, vindexB512);
            maskB512 = _mm512_cmplt_epi32_mask(vindexBInner512, vindexBStop512);

            miAelems512 = _mm512_mask_i32gather_epi32(miAelems512, micmp512 & maskA512, vindexAInner512, (const int *)array + index, 4);
            miBelems512 = _mm512_mask_i32gather_epi32(miBelems512, (~micmp512) & maskB512, vindexBInner512, (const int *)array + index, 4);

            //compare the elements
            micmp512 = _mm512_mask_cmple_epi32_mask(maskA512, miAelems512, miBelems512);
            micmp512 |= ~maskB512;
            miCelems512 = _mm512_mask_blend_epi32(micmp512, miBelems512, miAelems512);
            _mm512_i32scatter_epi32((int *)C + index, vindexCInner512, miCelems512, 4);
        }

        // Merge using 256 bit vectors
        numberOfExtraElementsMain = array_length%(16 * sortedArraySize);
        maxValueMain = array_length - numberOfExtraElementsMain;
        for (; index < maxValueMain; index += 16 * sortedArraySize) {
            vindexAInner256 = vindexA256;
            vindexBInner256 = vindexB256;
            vindexCInner256 = vindexA256;
            miAelems256 = _mm256_i32gather_epi32(vindexAInner256, (const int *)array + index, 4);
            miBelems256 = _mm256_i32gather_epi32(vindexBInner256, (const int *)array + index, 4);

            //compare the elements
            micmp256 = _mm256_cmple_epi32_mask(miAelems256, miBelems256);
            //copy the elements to the final elements
            miCelems256 = _mm256_mask_blend_epi32(micmp256, miBelems256, miAelems256);
            _mm256_i32scatter_epi32((int *)C + index, vindexCInner256, miCelems256, 4);

            // increase indexes
            vindexAInner256 = _mm256_mask_add_epi32(vindexAInner256, micmp256, vindexAInner256, mione256);
            vindexBInner256 = _mm256_mask_add_epi32(vindexBInner256, ~micmp256, vindexBInner256, mione256);
            vindexCInner256 = _mm256_add_epi32(vindexCInner256, mione256);
            uint32_t l1Index = index + 8;
            for (; l1Index < index + 16 * sortedArraySize - 8; l1Index += 8) {
                maskA256 = _mm256_cmplt_epi32_mask(vindexAInner256, vindexB256);
                maskB256 = _mm256_cmplt_epi32_mask(vindexBInner256, vindexBStop256);
                miAelems256 = _mm256_mask_i32gather_epi32(miAelems256, micmp256 & maskA256, vindexAInner256, (const int *)array + index, 4);
                miBelems256 = _mm256_mask_i32gather_epi32(miBelems256, (~micmp256) & maskB256, vindexBInner256, (const int *)array + index, 4);

                micmp256 = _mm256_mask_cmple_epi32_mask(maskA256, miAelems256, miBelems256);
                micmp256 |= ~maskB256;
                //copy the elements to the final elements
                miCelems256 = _mm256_mask_blend_epi32(micmp256, miBelems256, miAelems256);
                _mm256_i32scatter_epi32((int *)C + index, vindexCInner256, miCelems256, 4);

                // increase indexes
                vindexAInner256 = _mm256_mask_add_epi32(vindexAInner256, micmp256, vindexAInner256, mione256);
                vindexBInner256 = _mm256_mask_add_epi32(vindexBInner256, ~micmp256, vindexBInner256, mione256);
                vindexCInner256 = _mm256_add_epi32(vindexCInner256, mione256);
            }
            maskA256 = _mm256_cmplt_epi32_mask(vindexAInner256, vindexB256);
            maskB256 = _mm256_cmplt_epi32_mask(vindexBInner256, vindexBStop256);

            miAelems256 = _mm256_mask_i32gather_epi32(miAelems256, micmp256 & maskA256, vindexAInner256, (const int *)array + index, 4);
            miBelems256 = _mm256_mask_i32gather_epi32(miBelems256, (~micmp256) & maskB256, vindexBInner256, (const int *)array + index, 4);

            //compare the elements
            micmp256 = _mm256_mask_cmple_epi32_mask(maskA256, miAelems256, miBelems256);
            micmp256 |= ~maskB256;
            miCelems256 = _mm256_mask_blend_epi32(micmp256, miBelems256, miAelems256);
            _mm256_i32scatter_epi32((int *)C + index, vindexCInner256, miCelems256, 4);
        }

        // Merge using 128 bit vectors
        numberOfExtraElementsMain = array_length%(8 * sortedArraySize);
        maxValueMain = array_length - numberOfExtraElementsMain;
        for (; index < maxValueMain; index += 8 * sortedArraySize) {
            vindexAInner128 = vindexA128;
            vindexBInner128 = vindexB128;
            vindexCInner128 = vindexA128;
            miAelems128 = _mm128_i32gather_epi32(vindexAInner128, (const int *)array + index, 4);
            miBelems128 = _mm128_i32gather_epi32(vindexBInner128, (const int *)array + index, 4);

            //compare the elements
            micmp128 = _mm128_cmple_epi32_mask(miAelems128, miBelems128);
            //copy the elements to the final elements
            miCelems128 = _mm128_mask_blend_epi32(micmp128, miBelems128, miAelems128);
            _mm128_i32scatter_epi32((int *)C + index, vindexCInner128, miCelems128, 4);

            // increase indexes
            vindexAInner128 = _mm128_mask_add_epi32(vindexAInner128, micmp128, vindexAInner128, mione128);
            vindexBInner128 = _mm128_mask_add_epi32(vindexBInner128, ~micmp128, vindexBInner128, mione128);
            vindexCInner128 = _mm128_add_epi32(vindexCInner128, mione128);
            uint32_t l1Index = index + 4;
            for (; l1Index < index + 8 * sortedArraySize - 4; l1Index += 4) {
                maskA128 = _mm128_cmplt_epi32_mask(vindexAInner128, vindexB128);
                maskB128 = _mm128_cmplt_epi32_mask(vindexBInner128, vindexBStop128);
                miAelems128 = _mm128_mask_i32gather_epi32(miAelems128, micmp128 & maskA128, vindexAInner128, (const int *)array + index, 4);
                miBelems128 = _mm128_mask_i32gather_epi32(miBelems128, (~micmp128) & maskB128, vindexBInner128, (const int *)array + index, 4);

                micmp128 = _mm128_mask_cmple_epi32_mask(maskA128, miAelems128, miBelems128);
                micmp128 |= ~maskB128;
                //copy the elements to the final elements
                miCelems128 = _mm128_mask_blend_epi32(micmp128, miBelems128, miAelems128);
                _mm128_i32scatter_epi32((int *)C + index, vindexCInner128, miCelems128, 4);

                // increase indexes
                vindexAInner128 = _mm128_mask_add_epi32(vindexAInner128, micmp128, vindexAInner128, mione128);
                vindexBInner128 = _mm128_mask_add_epi32(vindexBInner128, ~micmp128, vindexBInner128, mione128);
                vindexCInner128 = _mm128_add_epi32(vindexCInner128, mione128);
            }
            maskA128 = _mm128_cmplt_epi32_mask(vindexAInner128, vindexB128);
            maskB128 = _mm128_cmplt_epi32_mask(vindexBInner128, vindexBStop128);

            miAelems128 = _mm128_mask_i32gather_epi32(miAelems128, micmp128 & maskA128, vindexAInner128, (const int *)array + index, 4);
            miBelems128 = _mm128_mask_i32gather_epi32(miBelems128, (~micmp128) & maskB128, vindexBInner128, (const int *)array + index, 4);

            //compare the elements
            micmp128 = _mm128_mask_cmple_epi32_mask(maskA128, miAelems128, miBelems128);
            micmp128 |= ~maskB128;
            miCelems128 = _mm128_mask_blend_epi32(micmp128, miBelems128, miAelems128);
            _mm128_i32scatter_epi32((int *)C + index, vindexCInner128, miCelems128, 4);
        }

        // TODO now merge the rest
        numberOfExtraElementsMain = array_length%(2 * sortedArraySize);
        maxValueMain = array_length - numberOfExtraElementsMain;
        for (; index < maxValueMain; index += 2 * sortedArraySize) {
            uint32_t Aindex=0,Bindex=0,Cindex=0;
            while(Aindex < sortedArraySize && Bindex < sortedArraySize) {
                //printf("10\n");
                (C + index)[Cindex++] = (array + index)[Aindex] < (array + index + sortedArraySize)[Bindex] ? (array + index)[Aindex++] : (array + index + sortedArraySize)[Bindex++];
            }
            while(Aindex < sortedArraySize) (C + index)[Cindex++] = (array + index)[Aindex++];
            while(Bindex < sortedArraySize) (C + index)[Cindex++] = (array + index + sortedArraySize)[Bindex++];
        }


        // Pointer Swap
        vec_t* tmp = array;
        array = C;
        C = tmp;
        numberOfSwaps++;
    }

    for (; sortedArraySize < array_length; sortedArraySize <<= 1) {
        for (uint32_t A_start = 0; A_start < array_length; A_start += 2 * sortedArraySize)
    	{
            uint32_t A_end = min(A_start + sortedArraySize, array_length - 1);
    		uint32_t B_start = A_end;
    		uint32_t B_end = min(A_start + 2 * sortedArraySize, array_length);
            uint32_t A_length = A_end - A_start;
            uint32_t B_length = B_end - B_start;

            struct AlgoArgs mergeArgs;
            mergeArgs.A = array + A_start;
            mergeArgs.A_length = A_length;
            mergeArgs.B = array + B_start;
            mergeArgs.B_length = B_length;
            mergeArgs.C = C + A_start;
            mergeArgs.C_length = A_length + B_length;

            Merge(&mergeArgs);
    	}
        //pointer swap for C
        vec_t* tmp = array;
        array = C;
        C = tmp;
        numberOfSwaps++;
    }

    if (numberOfSwaps%2 == 1) {
        memcpy((void*)C,(void*)array, (array_length)*sizeof(vec_t));
        vec_t* tmp = array;
        array = C;
        C = tmp;
    }
}


template <AlgoTemplate Merge>
void iterativeMergeSort(struct AlgoArgs *args) {
    vec_t* C = args->C;
    uint32_t C_length = args->C_length;
    vec_t* array = args->CUnsorted;
    uint32_t array_length = args->C_length;

    // We can't return different pointers because
    // this will cause memory issues.
    // Therefore we might need to copy in the last step
    int numberOfSwaps = 0;

    // Sort in blocks of 64 to start
    uint32_t subArraySize = 64;
    for (uint32_t i = 0; i < array_length; i += subArraySize) {
        // Adjust when array_length is not divisible by subArraySize
        uint32_t actualSubArraySize = min(subArraySize, array_length - i);
        qsort((void*)(array + i), actualSubArraySize, sizeof(vec_t), hostBasicCompare);
    }

    //now do actual iterative merge sort
    for (uint32_t currentSubArraySize = subArraySize; currentSubArraySize < array_length; currentSubArraySize = 2 * currentSubArraySize)
    {
    	for (uint32_t A_start = 0; A_start < array_length; A_start += 2 * currentSubArraySize)
    	{
            uint32_t A_end = min(A_start + currentSubArraySize, array_length - 1);
    		uint32_t B_start = A_end;
    		uint32_t B_end = min(A_start + 2 * currentSubArraySize, array_length);
            uint32_t A_length = A_end - A_start;
            uint32_t B_length = B_end - B_start;

            struct AlgoArgs mergeArgs;
            mergeArgs.A = array + A_start;
            mergeArgs.A_length = A_length;
            mergeArgs.B = array + B_start;
            mergeArgs.B_length = B_length;
            mergeArgs.C = C + A_start;
            mergeArgs.C_length = A_length + B_length;

           Merge(&mergeArgs);
    	}
        // Pointer swap for C
        vec_t* tmp = array;
        array = C;
        C = tmp;
        numberOfSwaps++;
    }

    if (numberOfSwaps%2 == 1) {
        memcpy((void*)C,(void*)array, (array_length)*sizeof(vec_t));
        vec_t* tmp = array;
        array = C;
        C = tmp;
    }
}

/*
 * Sums the values of the array up to and not including the given index
 */
inline uint32_t arraySum(uint32_t* array, uint32_t sumToIndex) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < sumToIndex; i++) {
        sum += array[i];
    }
    return sum;
}

template <AlgoTemplate Sort, AlgoTemplate Merge>
void parallelIterativeMergeSort(struct AlgoArgs *args) {
    int numberOfSwaps = 0;

    vec_t* C = args->C;
    uint32_t C_length = args->C_length;
    vec_t* array = args->CUnsorted;
    uint32_t array_length = args->C_length;

    #pragma omp parallel
    {
            uint32_t threadNum = omp_get_thread_num();
            uint32_t numberOfThreads = omp_get_num_threads();
            uint32_t* ASplitters = args->ASplitters + (numberOfThreads + 1) * threadNum;
            uint32_t* BSplitters = args->BSplitters + (numberOfThreads + 1) * threadNum;
            uint32_t* arraySizes = args->arraySizes + numberOfThreads * threadNum;
            uint32_t numberOfSubArrays = numberOfThreads;
            uint32_t initialSubArraySize = array_length / numberOfThreads;

            //Calculate the size of each subarray
            for (uint32_t thread = 0; thread < (uint32_t)numberOfThreads; thread++) {
                arraySizes[thread] = initialSubArraySize;
                if (((thread % 2) == 1) && thread < 2*(array_length % (uint32_t)numberOfThreads)) {
                    arraySizes[thread]++;
                } else if ((array_length % (uint32_t)numberOfThreads) > numberOfSubArrays/2 && thread < 2*((array_length % (uint32_t)numberOfThreads) - numberOfSubArrays/2)) {
                    arraySizes[thread]++;
                }
            }

            uint32_t threadStartIndex = arraySum(arraySizes, threadNum);
            uint32_t currentSubArraySize = arraySizes[threadNum];

            struct AlgoArgs mergeArgs;
            mergeArgs.C = C + threadStartIndex;
            mergeArgs.C_length = currentSubArraySize;
            mergeArgs.CUnsorted = array + threadStartIndex;

            // Each thread does its own sort
            Sort(&mergeArgs);

            uint32_t leftOverThreadsCounter, groupNumber, mergeHeadThreadNum, arraySizesIndex, numPerMergeThreads, leftOverThreads, deferedSubArray = 0, deferedSize = 0;

            //check if odd number of subarrays
            if (numberOfSubArrays % 2 == 1) {
                deferedSubArray = 1; //acts like a boolean
                deferedSize = arraySizes[numberOfSubArrays - 1];
                numberOfSubArrays--;
            }

            //begin merging
            #pragma omp barrier
            while (currentSubArraySize < array_length && (numberOfSubArrays > 1 || deferedSubArray)) {
                currentSubArraySize = arraySizes[0];
                numPerMergeThreads = numberOfThreads/(numberOfSubArrays/2);
                leftOverThreads = numberOfThreads%(numberOfSubArrays/2);

                //determines which threads will merge which sub arrays
                leftOverThreadsCounter = leftOverThreads;
                groupNumber = 0;
                mergeHeadThreadNum = 0;
                for (uint32_t i = 0; i < (uint32_t)numberOfThreads && numPerMergeThreads != 0; ) {
                    if (leftOverThreadsCounter) {
                        leftOverThreadsCounter--;
                        i += (numPerMergeThreads + 1);
                    } else {
                        i += numPerMergeThreads;
                    }
                    if (threadNum < i) {
                        break;
                    }
                    mergeHeadThreadNum = i;
                    groupNumber++;
                }

                arraySizesIndex = groupNumber*2; //points to index of A in array sizes for this thread

                //now asign left over threads starting at the front
                for (uint32_t i = 0; i < leftOverThreads; i++) {
                    if (threadNum >= (numPerMergeThreads+1)*i && threadNum < (numPerMergeThreads+1)*(i+1)) {
                        numPerMergeThreads++;
                    }
                }

                uint32_t AStartMergePath = arraySum(arraySizes, arraySizesIndex);
                uint32_t BStartMergePath = AStartMergePath + arraySizes[arraySizesIndex];

                MergePathSplitter(
                    array + AStartMergePath, arraySizes[arraySizesIndex],
                    array + BStartMergePath, arraySizes[arraySizesIndex + 1],
                    C + AStartMergePath, arraySizes[arraySizesIndex] + arraySizes[arraySizesIndex + 1],
                    numPerMergeThreads,
                    ASplitters + mergeHeadThreadNum, BSplitters + mergeHeadThreadNum); //Splitters[subArrayStart thread num] should be index zero

                uint32_t A_start = AStartMergePath + ASplitters[threadNum];
                uint32_t A_end = AStartMergePath + ASplitters[threadNum + 1];
                uint32_t A_length = A_end - A_start;
                uint32_t B_start = BStartMergePath + BSplitters[threadNum];
                uint32_t B_end = BStartMergePath + BSplitters[threadNum + 1];
                uint32_t B_length = B_end - B_start;
                uint32_t C_start = ASplitters[threadNum] + BSplitters[threadNum] + AStartMergePath; //start C at offset of previous
                uint32_t C_length = A_length + B_length;

                struct AlgoArgs mergeArgs;
                mergeArgs.A = array + A_start;
                mergeArgs.A_length = A_length;
                mergeArgs.B = array + B_start;
                mergeArgs.B_length = B_length;
                mergeArgs.C = C + C_start;
                mergeArgs.C_length = A_length + B_length;

                Merge(&mergeArgs);

                //number of sub arrays is now cut in half
                numberOfSubArrays = numberOfSubArrays/2;

                //Add up array sizes
                for (uint32_t i = 0, index = 0; i < numberOfSubArrays; i++) {
                    arraySizes[i] = arraySizes[index] + arraySizes[index + 1];
                    index += 2;
                }

                //Take care of odd sized number of sub arrays
                //If there is a defered sub array, we need to
                //Manually copy it here
                if (numberOfSubArrays % 2 == 1 && deferedSubArray) {
                    #pragma omp single
                    {
                        memcpy((void*)(C+array_length-deferedSize),
                            (void*)(array+array_length-deferedSize),
                            deferedSize*sizeof(vec_t));
                    }
                    deferedSubArray = 0;
                    arraySizes[numberOfSubArrays] = deferedSize;
                    numberOfSubArrays++;
                } else if (numberOfSubArrays % 2 == 1 && numberOfSubArrays != 1) {
                    deferedSubArray = 1;
                    deferedSize = arraySizes[numberOfSubArrays - 1];
                    numberOfSubArrays--;
                } else if (deferedSubArray) {
                    #pragma omp single
                    {
                        memcpy((void*)(C+array_length-deferedSize),
                            (void*)(array+array_length-deferedSize),
                            deferedSize*sizeof(vec_t));
                    }
                }

                //swap pointers
                #pragma omp barrier
                #pragma omp single
                {
                    vec_t* tmp = array;
                    array = C;
                    C = tmp;
                    numberOfSwaps++;
                }
            }
    }
    //must return original array
    if (numberOfSwaps > 0 && numberOfSwaps%2 == 1) {
        memcpy((void*)C,(void*)array, (array_length+32)*sizeof(vec_t));
        vec_t* tmp = array;
        array = C;
        C = tmp;
    }
}

template <AlgoTemplate Sort, AlgoTemplate Merge>
void parallelIterativeMergeSortPower2(struct AlgoArgs *args) {
    int numberOfSwaps = 0;
    vec_t* C = args->C;
    uint32_t C_length = args->C_length;
    vec_t* array = args->CUnsorted;
    uint32_t array_length = args->C_length;
    #pragma omp parallel
    {
        uint32_t threadNum = omp_get_thread_num();
        uint32_t numberOfThreads = omp_get_num_threads();

        // Initialize each threads memory
        uint32_t* ASplitters = args->ASplitters + (numberOfThreads + 1) * threadNum;
        uint32_t* BSplitters = args->BSplitters + (numberOfThreads + 1) * threadNum;

        uint32_t subArraySize = array_length / numberOfThreads;

        struct AlgoArgs mergeArgs;
        mergeArgs.C = C + subArraySize*threadNum;
        mergeArgs.C_length = subArraySize;
        mergeArgs.CUnsorted = array + subArraySize*threadNum;

        // Each thread does its own sort
        Sort(&mergeArgs);

        uint32_t numberOfSubArrays = numberOfThreads;

        #pragma omp barrier
        // Begin merging
        while (subArraySize < array_length) {
            uint32_t numPerMergeThreads = numberOfThreads/(numberOfSubArrays/2);
            uint32_t mergeGroupNumber = threadNum/numPerMergeThreads;

            MergePathSplitterThread(
                array + subArraySize*mergeGroupNumber*2, subArraySize,
                array + subArraySize*mergeGroupNumber*2 + subArraySize, subArraySize,
                C + subArraySize*mergeGroupNumber*2, subArraySize*2,
                numPerMergeThreads,
                ASplitters, BSplitters, threadNum%numPerMergeThreads);

            uint32_t A_start = subArraySize*mergeGroupNumber*2 + ASplitters[threadNum%numPerMergeThreads];
            uint32_t A_end = subArraySize*mergeGroupNumber*2 + ASplitters[threadNum%numPerMergeThreads + 1];
            uint32_t A_length = A_end - A_start;
            uint32_t B_start = subArraySize*mergeGroupNumber*2 + subArraySize + BSplitters[threadNum%numPerMergeThreads];
            uint32_t B_end = subArraySize*mergeGroupNumber*2 + subArraySize + BSplitters[threadNum%numPerMergeThreads + 1];
            uint32_t B_length = B_end - B_start;
            uint32_t C_start = ASplitters[threadNum%numPerMergeThreads] + BSplitters[threadNum%numPerMergeThreads] + subArraySize*mergeGroupNumber*2;
            uint32_t C_length = A_length + B_length;

            struct AlgoArgs mergeArgs;
            mergeArgs.A = array + A_start;
            mergeArgs.A_length = A_length;
            mergeArgs.B = array + B_start;
            mergeArgs.B_length = B_length;
            mergeArgs.C = C + C_start;
            mergeArgs.C_length = A_length + B_length;

            Merge(&mergeArgs);

            numberOfSubArrays /= 2;
            subArraySize *= 2;

            //swap pointers
            #pragma omp barrier
            #pragma omp single
            {
                vec_t* tmp = array;
                array = C;
                C = tmp;
                numberOfSwaps++;
            }
        }
    }

    //must return original array
    if (numberOfSwaps > 0 && numberOfSwaps%2 == 1) {
        memcpy((void*)C,(void*)array, (array_length+32)*sizeof(vec_t));
        vec_t* tmp = array;
        array = C;
        C = tmp;
    }
}

/*
 * Template Instantiations
 */

// Sorts
template void iterativeMergeSort<serialMerge>(struct AlgoArgs *args);
template void iterativeMergeSort<bitonicMergeReal>(struct AlgoArgs *args);
template void iterativeMergeSort<avx512Merge>(struct AlgoArgs *args);
template void avx512SortNoMergePathV2<bitonicMergeReal>(struct AlgoArgs *args);
template void avx512SortNoMergePathV2<avx512Merge>(struct AlgoArgs *args);
// Parallel Merge
template void parallelMerge<serialMerge>(struct AlgoArgs *args);
template void parallelMerge<bitonicMergeReal>(struct AlgoArgs *args);
template void parallelMerge<avx512Merge>(struct AlgoArgs *args);
// Parallel Sort
template void parallelIterativeMergeSort<iterativeMergeSort<serialMerge>,serialMerge>(struct AlgoArgs *args);
template void parallelIterativeMergeSort<iterativeMergeSort<bitonicMergeReal>,bitonicMergeReal>(struct AlgoArgs *args);
template void parallelIterativeMergeSort<avx512SortNoMergePathV2<avx512Merge>,avx512Merge>(struct AlgoArgs *args);
template void parallelIterativeMergeSortPower2<iterativeMergeSort<serialMerge>,serialMerge>(struct AlgoArgs *args);
template void parallelIterativeMergeSortPower2<iterativeMergeSort<bitonicMergeReal>,bitonicMergeReal>(struct AlgoArgs *args);
template void parallelIterativeMergeSortPower2<avx512SortNoMergePathV2<avx512Merge>,avx512Merge>(struct AlgoArgs *args);
