#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <iostream>
#include <array>

#include "common.h"

#define CUDA_BLOCKS 160
#define CUDA_THREADS 1024
#define CUDA_STREAMS 4

#ifndef CUDA_BLOCKS
#define CUDA_BLOCKS 160
#endif


#ifndef CUDA_THREADS 
#define CUDA_THREADS 1024
#endif

#ifndef CUDA_STREAMS 
#define CUDA_STREAMS 4
#endif

#define BITMAP_POWER 12

#define CUDA_CHECK \
        do { } while (false)
        //do { std::cout << "line: " << __LINE__ << " " << cudaGetLastError() << std::endl; } while (false)

using count_t = unsigned long long;

__device__ static void bitmap_set(uint32_t* bm0, uint32_t* bm1, const vertex_t vid) {
	atomicOr(&bm0[vid >> BITMAP_POWER], 1 << ((vid >> (BITMAP_POWER-5)) & 31));
	atomicOr(&bm1[vid >> 5], 1 << (vid & 31));
}

__device__ static bool bitmap_check(uint32_t* bm0, uint32_t * bm1, const vertex_t vid) {
    if (bm0[vid >> BITMAP_POWER] & (1 << ((vid >> (BITMAP_POWER - 5) & 31)))) {
        return bm1[vid >> 5] & (1 << (vid & 31));
    } else {
        return false;
    }
}

__global__ static void kernel(
    vertex_t const * G0Row, vertex_t const * G0Ptr, vertex_t const * G0Col,
    vertex_t const * G1Row, vertex_t const * G1Ptr, vertex_t const * G1Col,
    vertex_t const * G2Row, vertex_t const * G2Ptr, vertex_t const * G2Col,
    count_t const G1RowSz,
    uint32_t * bitmap_lv0, uint32_t * bitmap_lv1,
    count_t * count
) {
    uint32_t * mybm0 = &bitmap_lv0[(FORMAT_GRID_WIDTH >> BITMAP_POWER) * blockIdx.x];
    uint32_t * mybm1 = &bitmap_lv1[(FORMAT_GRID_WIDTH >> 5) * blockIdx.x];
    count_t mycount = 0;

    __shared__ int SHARED[CUDA_THREADS];

    for (uint32_t g1row_iter = blockIdx.x; g1row_iter < G1RowSz; g1row_iter += gridDim.x) {
        auto const g1row = G1Row[g1row_iter]; // This makes huge difference!!!
        // Without "Existing Row" information: loop all 2^24 and check it all
        // With "Existing Row" information: extremely faster than without-version
        auto const g1col_idx_s = G1Ptr[g1row];
        auto const g1col_idx_e = G1Ptr[g1row+1];

        // generate bitmap
        for (uint32_t g1col_idx = g1col_idx_s + threadIdx.x; g1col_idx < g1col_idx_e; g1col_idx += blockDim.x) {
            bitmap_set(mybm0, mybm1, G1Col[g1col_idx]);
        }

        auto g2col_s = G2Ptr[g1row];
        auto g2col_e = G2Ptr[g1row+1];

        for (uint32_t g2col_idx = g2col_s; g2col_idx < g2col_e; g2col_idx += blockDim.x) {
            SHARED[threadIdx.x] = (g2col_idx + threadIdx.x < g2col_e) ? (int)G2Col[g2col_idx + threadIdx.x] : -1;

            __syncthreads();

            for (uint32_t s = 0; s < blockDim.x; s++) {
                int const g2col = SHARED[s];
                if (g2col == -1) { break; }

                auto const g0col_idx_s = G0Ptr[g2col];
                auto const g0col_idx_e = G0Ptr[g2col+1];

                for (uint32_t g0col_idx = g0col_idx_s + threadIdx.x; g0col_idx < g0col_idx_e; g0col_idx += blockDim.x) {
                    if (bitmap_check(mybm0, mybm1, G0Col[g0col_idx])) {
                        mycount++;
                    }
                }
            }
            __syncthreads();
        }

        // reset bitmap
        for (uint32_t g1col_idx = g1col_idx_s + threadIdx.x; g1col_idx < g1col_idx_e; g1col_idx += blockDim.x) {
            auto const c = G1Col[g1col_idx];
            mybm0[c >> BITMAP_POWER] = 0;
            mybm1[c >> 5] = 0;
        }

        __syncthreads();
    }

    //atomicAdd(count, mycount);

    for (uint8_t offset = 16; offset > 0; offset >>= 1) {
		mycount += __shfl_down_sync(0xFFFFFFFF, mycount, offset);
	}

	if ((threadIdx.x & 31) == 0) { atomicAdd(count, mycount); }
}

void launch(std::vector<gridInfo_t> const & info, std::vector<gridData_t> & data) {
    //std::cout << ">>> Launch GPU" << std::endl;
    std::cout << "STREAMS: " << CUDA_STREAMS << ", BLOCKS: " << CUDA_BLOCKS << ", THREADS: " << CUDA_THREADS << std::endl;

    auto rows = info.back().pos.row + 1;
    auto cols = info.back().pos.col + 1;
    auto rc2i = [&cols](vertex_t const row, vertex_t const col) ->vertex_t{ return row * cols + col; };

    std::vector<std::vector<vertex_t*>> dRow, dPtr, dCol;

    dRow.resize(rows);
    for (auto & e : dRow) { e.resize(cols); }

    dPtr.resize(rows);
    for (auto & e : dPtr) { e.resize(cols); }

    dCol.resize(rows);
    for (auto & e : dCol) { e.resize(cols); }

    cudaDeviceReset(); CUDA_CHECK;
    cudaSetDevice(0); CUDA_CHECK;

    std::array<count_t *, CUDA_STREAMS> dcount;
    for (uint32_t i = 0; i < CUDA_STREAMS; i++) {
        cudaMalloc(&dcount[i], sizeof(count_t)); CUDA_CHECK;
        cudaMemset(dcount[i], 0, sizeof(count_t)); CUDA_CHECK;
    }

    std::array<uint32_t *, CUDA_STREAMS> bitmap_lv1;
    for (uint32_t i = 0; i < CUDA_STREAMS; i++) {
        cudaMalloc(&bitmap_lv1[i], sizeof(uint32_t) * (FORMAT_GRID_WIDTH >> 5) * CUDA_BLOCKS); CUDA_CHECK;
        cudaMemset(bitmap_lv1[i], 0, sizeof(uint32_t) * (FORMAT_GRID_WIDTH >> 5) * CUDA_BLOCKS); CUDA_CHECK;
    }

    std::array<uint32_t *, CUDA_STREAMS> bitmap_lv0;
    for (uint32_t i = 0; i < CUDA_STREAMS; i++) {
        cudaMalloc(&bitmap_lv0[i], sizeof(uint32_t) * (FORMAT_GRID_WIDTH >> BITMAP_POWER) * CUDA_BLOCKS); CUDA_CHECK;
        cudaMemset(bitmap_lv0[i], 0, sizeof(uint32_t) * (FORMAT_GRID_WIDTH >> BITMAP_POWER) * CUDA_BLOCKS); CUDA_CHECK;
    }


    //std::cout << "complete: GPU bitmap malloc & memset" << std::endl;

    for (uint32_t row = 0; row < rows; row++) {
        for (uint32_t col = 0; col <= row; col++) {
            auto idx = rc2i(row, col);
            cudaMalloc(&dRow[row][col], data[idx].row.size() * sizeof(vertex_t)); CUDA_CHECK;
            cudaMalloc(&dPtr[row][col], data[idx].ptr.size() * sizeof(vertex_t)); CUDA_CHECK;
            cudaMalloc(&dCol[row][col], data[idx].col.size() * sizeof(vertex_t)); CUDA_CHECK;
        }
    }

    //std::cout << "complete: GPU graph data malloc" << std::endl;

    for (uint32_t row = 0; row < rows; row++) {
        for (uint32_t col = 0; col <= row; col++) {
            auto idx = rc2i(row, col);
            cudaMemcpy(dRow[row][col], data[idx].row.data(), data[idx].row.size() * sizeof(vertex_t), cudaMemcpyHostToDevice); CUDA_CHECK;
            cudaMemcpy(dPtr[row][col], data[idx].ptr.data(), data[idx].ptr.size() * sizeof(vertex_t), cudaMemcpyHostToDevice); CUDA_CHECK;
            cudaMemcpy(dCol[row][col], data[idx].col.data(), data[idx].col.size() * sizeof(vertex_t), cudaMemcpyHostToDevice); CUDA_CHECK;
        }
    }

/*
    cudaDeviceSynchronize();

    for (uint32_t row = 0; row < rows; row++) {
        for (uint32_t col = 0; col <= row; col++) {
            auto idx = rc2i(row, col);
            data[idx].row.resize(0);
            data[idx].ptr.resize(0);
            data[idx].col.resize(0);
        }
    }
    */

    //std::cout << "complete: GPU graph data memcpy" << std::endl;

    std::array<cudaStream_t, CUDA_STREAMS> stream;
    for (uint32_t i = 0; i < CUDA_STREAMS; i++) {
        cudaStreamCreate(&stream[i]); CUDA_CHECK;
    }

    //std::cout << "complete: GPU stream create" << std::endl;

    std::array<count_t, CUDA_STREAMS> count = {0, };

    auto start = std::chrono::system_clock::now();


    uint32_t stream_number = 0;
    for (uint32_t row = 0; row < rows; row++) {
        for (uint32_t col = 0; col <= row; col++) {
            for (uint32_t i = col; i <= row; i++) {
                kernel <<<CUDA_BLOCKS, CUDA_THREADS, 0, stream[stream_number]>>> (
                    dRow[i][col], dPtr[i][col],   dCol[i][col],
                    dRow[row][col], dPtr[row][col], dCol[row][col],
                    dRow[row][i], dPtr[row][i],   dCol[row][i],
                    data[rc2i(row, col)].row.size(),
                    bitmap_lv0[stream_number],
                    bitmap_lv1[stream_number],
                    dcount[stream_number]
                );

                stream_number++;
                if (stream_number / CUDA_STREAMS != 0) {
                    stream_number = 0;
                }
            }
        }
    }

    for (uint32_t i = 0; i < CUDA_STREAMS; i++) {
        cudaMemcpyAsync(&count[i], dcount[i], sizeof(count_t), cudaMemcpyDeviceToHost, stream[i]); CUDA_CHECK;
        cudaStreamSynchronize(stream[i]); CUDA_CHECK;
    }

    for (uint32_t i = 1; i < CUDA_STREAMS; i++) {
        count[0] += count[i];
    }

    std::chrono::duration<double> elapsed = std::chrono::system_clock::now() - start;
    std::cout << count[0] << "," << elapsed.count() << std::endl;

    for (uint32_t i = 0; i < CUDA_STREAMS; i++) {
        cudaStreamDestroy(stream[i]); CUDA_CHECK;
    }

    cudaDeviceReset(); CUDA_CHECK;
}