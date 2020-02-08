/*******************************************************************************************

 Nexus Earth 2018

 (credits: cbuchner1 for sieving)

 [Scale Indefinitely] BlackJack. http://www.opensource.org/licenses/mit-license.php

*******************************************************************************************/

#include <CUDA/include/macro.h>
#include <CUDA/include/sieve.h>
#include <CUDA/include/util.h>
#include <CUDA/include/frame_resources.h>
#include <CUDA/include/sieve_resources.h>
#include <CUDA/include/streams_events.h>

#include <CUDA/include/combo_sieve.h>

#include <CUDA/include/constants.h>
#include <CUDA/include/unroller.cuh>

#include <Util/include/debug.h>

#include <cuda.h>
#include <stdio.h>



struct FrameResource frameResources[GPU_MAX];
uint4 *d_primesInverseInvk[GPU_MAX];
uint64_t *d_origins[GPU_MAX];
uint32_t *d_primes[GPU_MAX];
uint32_t *d_prime_remainders[GPU_MAX];
uint32_t *d_base_remainders[GPU_MAX];
uint16_t *d_blockoffset_mod_p[GPU_MAX];
uint32_t nOffsetsA;
uint32_t nOffsetsB;


extern "C" void cuda_set_offset_patterns(uint8_t thr_id,
                                         const std::vector<uint32_t> &offsets,
                                         const std::vector<uint32_t> &indicesA,
                                         const std::vector<uint32_t> &indicesB,
                                         const std::vector<uint32_t> &indicesT)
{
    nOffsetsA = indicesA.size();
    nOffsetsB = indicesB.size();
    uint32_t nOffsetsT = indicesT.size();
    uint32_t nOffsets = offsets.size();
    uint32_t bitMaskA = 0;
    uint32_t bitMaskT = 0;

    if(nOffsets > OFFSETS_MAX)
    {
        debug::error(FUNCTION, "Cannot have more than 32 total offsets.");
        return;
    }

    if(nOffsetsA > 8 || nOffsetsB > 8 || nOffsetsT > 8)
    {
        debug::error(FUNCTION, "Cannot have more than 8 offsets");
        return;
    }

    /* Find the start and end indices for all GPU offsets. */
    uint32_t ibeg = 32;
    uint32_t iend = 0;

    for(uint8_t i = 0; i < nOffsetsA; ++i)
    {
        bitMaskA |= (1 << indicesA[i]);
        ibeg = std::min(ibeg, indicesA[i]);
        iend = std::max(iend, indicesA[i]);
    }
    for(uint8_t i = 0; i < nOffsetsB; ++i)
    {
        ibeg = std::min(ibeg, indicesB[i]);
        iend = std::max(iend, indicesB[i]);
    }

    for(uint8_t i = 0; i < nOffsetsT; ++i)
    {
        bitMaskT |= (1 << indicesT[i]);
    }

    CHECK(cudaMemcpyToSymbol(c_bitmaskT, &bitMaskT,
         sizeof(uint32_t), 0, cudaMemcpyHostToDevice));

    CHECK(cudaMemcpyToSymbol(c_bitmaskA, &bitMaskA,
         sizeof(uint32_t), 0, cudaMemcpyHostToDevice));

    CHECK(cudaMemcpyToSymbol(c_offsets, offsets.data(),
        nOffsets*sizeof(uint32_t), 0, cudaMemcpyHostToDevice));

    CHECK(cudaMemcpyToSymbol(c_iA, indicesA.data(),
        nOffsetsA*sizeof(uint32_t), 0, cudaMemcpyHostToDevice));

    CHECK(cudaMemcpyToSymbol(c_iB, indicesB.data(),
        nOffsetsB*sizeof(uint32_t), 0, cudaMemcpyHostToDevice));

    CHECK(cudaMemcpyToSymbol(c_iT, indicesT.data(),
        nOffsetsT*sizeof(uint32_t), 0, cudaMemcpyHostToDevice));

    CHECK(cudaMemcpyToSymbol(c_iBeg, &ibeg, sizeof(uint32_t), 0, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpyToSymbol(c_iEnd, &iend, sizeof(uint32_t), 0, cudaMemcpyHostToDevice));
}


extern "C" void cuda_set_zTempVar(uint8_t thr_id, const uint64_t *limbs)
{
    CHECK(cudaMemcpyToSymbol(c_zTempVar, limbs, 17*sizeof(uint64_t), 0, cudaMemcpyHostToDevice));
}


extern "C" void cuda_init_primes(uint8_t thr_id,
                                 uint64_t *origins,
                                 uint32_t *primes,
                                 uint32_t *primesInverseInvk,
                                 uint32_t nPrimeLimit,
                                 uint32_t nBitArray_Size,
                                 uint32_t sharedSizeKB,
                                 uint32_t nPrimorialEndPrime,
                                 uint32_t nPrimeLimitA,
                                 uint32_t nOrigins,
                                 uint32_t nMaxCandidates)
{
    uint32_t primeinverseinvk_size = sizeof(uint32_t) * 4 * nPrimeLimit;
    uint64_t nonce64_size = nMaxCandidates * sizeof(uint64_t);
    uint64_t nonce32_size = nMaxCandidates * sizeof(uint32_t);
    uint32_t sharedSizeBits = sharedSizeKB * 1024 * 8;
    uint32_t allocSize = ((nBitArray_Size * 16  + sharedSizeBits - 1) / sharedSizeBits) * sharedSizeBits;
    uint32_t bitarray_size = (allocSize+31)/32 * sizeof(uint32_t);
    uint32_t remainder_size = 2 * 8 * 4096 * nOrigins * sizeof(uint32_t);

    /* Allocate memory for the primes, inverses, and reciprocals that are used
       as the basis for prime sieve computation */
    CHECK(cudaMalloc(&d_primesInverseInvk[thr_id],  primeinverseinvk_size));
    CHECK(cudaMemcpy(d_primesInverseInvk[thr_id], primesInverseInvk, primeinverseinvk_size, cudaMemcpyHostToDevice));

    /* Allocate base remainders that will be pre-computed once per block */
    CHECK(cudaMalloc(&d_base_remainders[thr_id],  nPrimeLimit * sizeof(uint32_t)));

    /* Create list of primes only */
    //CHECK(cudaMalloc(&d_primes[thr_id], nPrimeLimit * sizeof(uint32_t)));
    //CHECK(cudaMemcpy(d_primes[thr_id], primes, nPrimeLimit * sizeof(uint32_t), cudaMemcpyHostToDevice));


    /* Allocate memory for prime origins. */
    CHECK(cudaMalloc(&d_origins[thr_id], nOrigins * sizeof(uint64_t)));


    CHECK(cudaMalloc(&d_prime_remainders[thr_id], remainder_size));


    /* Allocate multiple frame resources so we can keep multiple frames in flight
       to further improve CPU/GPU utilization */

    for(uint8_t i = 0; i < FRAME_COUNT; ++i)
    {
        /* test */
        CHECK(    cudaMalloc(&frameResources[thr_id].d_result_offsets[i], nonce64_size));
        CHECK(cudaMallocHost(&frameResources[thr_id].h_result_offsets[i], nonce64_size));
        CHECK(    cudaMalloc(&frameResources[thr_id].d_result_meta[i],    nonce32_size));
        CHECK(cudaMallocHost(&frameResources[thr_id].h_result_meta[i],    nonce32_size));
        CHECK(    cudaMalloc(&frameResources[thr_id].d_result_count[i],   sizeof(uint32_t)));
        CHECK(cudaMallocHost(&frameResources[thr_id].h_result_count[i],   sizeof(uint32_t)));

        CHECK(    cudaMalloc(&frameResources[thr_id].d_window_data[i], nonce32_size * WORD_MAX * WINDOW_SIZE));

        /* test stats */
        CHECK(    cudaMalloc(&frameResources[thr_id].d_primes_checked[i], OFFSETS_MAX * sizeof(uint32_t)));
        CHECK(cudaMallocHost(&frameResources[thr_id].h_primes_checked[i], OFFSETS_MAX * sizeof(uint32_t)));
        CHECK(    cudaMalloc(&frameResources[thr_id].d_primes_found[i],   OFFSETS_MAX * sizeof(uint32_t)));
        CHECK(cudaMallocHost(&frameResources[thr_id].h_primes_found[i],   OFFSETS_MAX * sizeof(uint32_t)));

        /* compaction */
        CHECK(    cudaMalloc(&frameResources[thr_id].d_nonce_offsets[i], nonce64_size));
        CHECK(    cudaMalloc(&frameResources[thr_id].d_nonce_meta[i],    nonce32_size));
        CHECK(    cudaMalloc(&frameResources[thr_id].d_nonce_count[i],   sizeof(uint32_t)));

        CHECK(    cudaMalloc(&frameResources[thr_id].d_pre_nonce_offsets[i], nonce64_size));
        CHECK(    cudaMalloc(&frameResources[thr_id].d_pre_nonce_meta[i],    nonce32_size));
        CHECK(    cudaMalloc(&frameResources[thr_id].d_pre_nonce_count[i],   sizeof(uint32_t)));

        CHECK(cudaMallocHost(&frameResources[thr_id].h_nonce_count[i],   sizeof(uint32_t)));

        /* sieving */

        //CHECK(    cudaMalloc(&frameResources[thr_id].d_bit_array_sieve[i], bitarray_size));

        /* combo sieve */
        CHECK(    cudaMalloc(&frameResources[thr_id].d_bit_array_sieve[i], bitarray_size));


        /* bucket sieve (experimental) */
        // CHECK(cudaMalloc(&frameResources[thr_id].d_bucket_o[i], sizeof(uint32_t) * nPrimeLimit << 4));
        // CHECK(cudaMalloc(&frameResources[thr_id].d_bucket_away[i], sizeof(uint16_t) * nPrimeLimit << 4));
    }

    /* Have capacity for small primes up to 4096 */
    uint16_t p[4096];
    for(uint32_t i = 0; i < nPrimeLimitA; ++i)
        p[i] = primes[i];

    /* Copy small primes to GPU. */
    CHECK(cudaMemcpyToSymbol(c_primes, p, nPrimeLimitA * sizeof(uint16_t), 0, cudaMemcpyHostToDevice));

    /* Allocate and compute block offsets for a list of small prime mod offsets
       at each block offset in the gpu small sieve kernel */
    uint32_t nBlocks = (nBitArray_Size + sharedSizeBits-1) / sharedSizeBits;
    uint32_t blockoffset_size = nBlocks * 4096 * sizeof(uint16_t);

    CHECK(cudaMalloc(&d_blockoffset_mod_p[thr_id], blockoffset_size));

    uint16_t *offsets = (uint16_t *)malloc(blockoffset_size);

    for (uint32_t block = 0; block < nBlocks; ++block)
    {
        uint32_t blockOffset = sharedSizeBits * block;

        for (uint32_t i = 0; i < nPrimeLimitA; ++i)
            offsets[block*4096 + i] = primes[i] - (blockOffset % primes[i]);
    }
    CHECK(cudaMemcpy(d_blockoffset_mod_p[thr_id], offsets, blockoffset_size, cudaMemcpyHostToDevice));
    free(offsets);

    /* Create the CUDA streams and events used for sieve, compacting, and testing */
    streams_events_init(thr_id);
}

extern "C" void cuda_free_primes(uint8_t thr_id)
{
    CHECK(cudaFree(d_primesInverseInvk[thr_id]));
    CHECK(cudaFree(d_base_remainders[thr_id]));
    //CHECK(cudaFree(d_primes[thr_id]));
    CHECK(cudaFree(d_origins[thr_id]));
    CHECK(cudaFree(d_prime_remainders[thr_id]));

    for(uint8_t i = 0; i < FRAME_COUNT; ++i)
    {
        CHECK(    cudaFree(frameResources[thr_id].d_result_offsets[i]));
        CHECK(cudaFreeHost(frameResources[thr_id].h_result_offsets[i]));

        CHECK(    cudaFree(frameResources[thr_id].d_result_meta[i]));
        CHECK(cudaFreeHost(frameResources[thr_id].h_result_meta[i]));

        CHECK(    cudaFree(frameResources[thr_id].d_result_count[i]));
        CHECK(cudaFreeHost(frameResources[thr_id].h_result_count[i]));

        CHECK(    cudaFree(frameResources[thr_id].d_primes_checked[i]));
        CHECK(cudaFreeHost(frameResources[thr_id].h_primes_checked[i]));

        CHECK(    cudaFree(frameResources[thr_id].d_primes_found[i]));
        CHECK(cudaFreeHost(frameResources[thr_id].h_primes_found[i]));

        CHECK(    cudaFree(frameResources[thr_id].d_nonce_offsets[i]));
        CHECK(    cudaFree(frameResources[thr_id].d_nonce_meta[i]));
        CHECK(    cudaFree(frameResources[thr_id].d_nonce_count[i]));

        CHECK(    cudaFree(frameResources[thr_id].d_pre_nonce_offsets[i]));
        CHECK(    cudaFree(frameResources[thr_id].d_pre_nonce_meta[i]));
        CHECK(    cudaFree(frameResources[thr_id].d_pre_nonce_count[i]));

        CHECK(cudaFreeHost(frameResources[thr_id].h_nonce_count[i]));


        CHECK(    cudaFree(frameResources[thr_id].d_bit_array_sieve[i]));

        //CHECK(cudaFree(frameResources[thr_id].d_bucket_o[i]));
        //CHECK(cudaFree(frameResources[thr_id].d_bucket_away[i]));
    }

    CHECK(cudaFree(d_blockoffset_mod_p[thr_id]));

    streams_events_free(thr_id);
}


__global__ void base_remainders_kernel(uint4 *primes, uint32_t *base_remainders, uint32_t nPrimeLimit)
{
    uint32_t i = (blockDim.x * blockIdx.x + threadIdx.x);


    if (i < nPrimeLimit)
    {
        uint4 tmp = primes[i];
        base_remainders[i] = mpi_mod_int(c_zTempVar, tmp.x, make_uint64_t(tmp.z, tmp.w));
    }

}

extern "C" void cuda_base_remainders(uint8_t thr_id, uint32_t nPrimeLimit)
{
    int nThreadsPerBlock = 32;

    for(uint8_t i = 0; i < 4; ++i)
    {

        dim3 block(nThreadsPerBlock);

        int i_beg = i * nPrimeLimit / 4;
        int i_end = (i+1) * nPrimeLimit / 4;

        if(i == 3)
            i_end = nPrimeLimit;

        int nThreads = i_end - i_beg;
        int nBlocks = (nThreads + nThreadsPerBlock - 1) / nThreadsPerBlock;
        dim3 grid(nBlocks);

        CHECK(stream_wait_event(thr_id, 0, i, i));

        base_remainders_kernel<<<grid, block, 0, d_Streams[thr_id][i]>>>(&d_primesInverseInvk[thr_id][i_beg],
                                                &d_base_remainders[thr_id][i_beg],
                                                nThreads);

        CHECK(stream_signal_event(thr_id, 0, i, i));

    }
}


__global__ void primesieve_kernelA0(uint64_t *origins,
                                    uint4 *primes,
                                    uint32_t *prime_remainders,
                                    uint32_t *base_remainders,
                                    uint8_t nOffsets,
                                    uint16_t nOrigins,
                                    uint32_t nThreads)
{
    uint32_t g_idx = blockIdx.x * blockDim.x + threadIdx.x;

    if(g_idx < nThreads)
    {
        uint32_t p_idx = g_idx / nOrigins;
        uint64_t o_idx = g_idx % nOrigins;

        uint32_t j = ((o_idx << 12) + p_idx) << 3;


        o_idx = origins[o_idx] + base_remainders[p_idx];

        uint4 tmp = primes[p_idx];
        uint64_t recip = make_uint64_t(tmp.z, tmp.w);

        tmp.z = mod_p_small(o_idx, tmp.x, recip);

        uint32_t pr;

        for(uint8_t o = 0; o < nOffsets; ++o)
        {
            pr = tmp.z + c_offsets[c_iA[o]];
            if(pr >= tmp.x)
                pr -= tmp.x;

            prime_remainders[j + o] = mod_p_small((uint64_t)(tmp.x - pr)*tmp.y, tmp.x, recip);
        }
    }
}


__global__ void primesieve_kernelB0(uint64_t *origins,
                                    uint4 *primes,
                                    uint32_t *prime_remainders,
                                    uint32_t *base_remainders,
                                    uint8_t nOffsets,
                                    uint16_t nOrigins,
                                    uint32_t nThreads)
{
    uint32_t g_idx = blockIdx.x * blockDim.x + threadIdx.x;

    if(g_idx < nThreads)
    {
        uint32_t p_idx = g_idx / nOrigins;
        uint64_t o_idx = g_idx % nOrigins;

        uint32_t j = ((o_idx << 12) + p_idx) << 3;


        o_idx = origins[o_idx] + base_remainders[p_idx];

        uint4 tmp = primes[p_idx];
        uint64_t recip = make_uint64_t(tmp.z, tmp.w);

        tmp.z = mod_p_small(o_idx, tmp.x, recip);

        uint32_t pr;

        for(uint8_t o = 0; o < nOffsets; ++o)
        {
            pr = tmp.z + c_offsets[c_iB[o]];
            if(pr >= tmp.x)
                pr -= tmp.x;

            prime_remainders[j + o] = mod_p_small((uint64_t)(tmp.x - pr)*tmp.y, tmp.x, recip);
        }
    }
}


template<uint8_t offsetsA>
__global__ void primesieve_kernelA_512(uint32_t *g_bit_array_sieve,
                                       uint32_t *prime_remainders,
                                       uint16_t *blockoffset_mod_p,
                                       uint32_t base_index,
                                       uint16_t nPrimorialEndPrime,
                                       uint16_t nPrimeLimitA)
{
    extern __shared__ uint32_t shared_array_sieve[];
    uint32_t pIdx;
    uint32_t nAdd;
    uint32_t pre1[offsetsA];
    uint32_t index;
    uint16_t i, j;
    uint16_t pr, pre2;

    #pragma unroll 16
    for (int i= 0; i <  16; ++i)
        shared_array_sieve[threadIdx.x + (i << 9)] = 0;

    __syncthreads();

    base_index = base_index << 12;

    for (i = nPrimorialEndPrime; i < nPrimeLimitA; ++i)
    {
        pr = c_primes[i];
        pre2 = blockoffset_mod_p[(blockIdx.x << 12) + i];

        // precompute
        pIdx = threadIdx.x * pr;
        nAdd = (base_index + i) << 3;

        auto pre = [&pre1, &prime_remainders, &nAdd](uint32_t o)
        {
            pre1[o] = prime_remainders[nAdd + o]; // << 3 because we have space for 8 offsets
        };

        Unroller<0, offsetsA>::step(pre);

        nAdd = pr << 9;
        auto loop = [&pIdx, &nAdd, &pre1, &pre2, &pr, &index](uint32_t o)
        {
            index = pre1[o] + pre2;
            if(index >= pr)
                index = index - pr;

            index = index + pIdx;

            for(; index < 262144; index += nAdd)
            {
                atomicOr(&shared_array_sieve[index >> 5], 1 << (index & 31));
            }
        };

        Unroller<0, offsetsA>::step(loop);
    }

    __syncthreads();
    g_bit_array_sieve += (blockIdx.x << 13);

    #pragma unroll 16
    for (int i = 0; i < 16; ++i) // fixed value
    {
        j = threadIdx.x + (i << 9);
        //atomicOr(&g_bit_array_sieve[j], shared_array_sieve[j]);
        g_bit_array_sieve[j] = shared_array_sieve[j];
    }
}


template<uint8_t offsetsA>
__global__ void primesieve_kernelD_512(uint32_t *g_bit_array_sieve,
                                       uint32_t *prime_remainders,
                                       uint16_t *blockoffset_mod_p,
                                       uint32_t base_index,
                                       uint16_t nPrimorialEndPrime,
                                       uint16_t nPrimeLimitA)
{
    extern __shared__ uint32_t shared_array_sieve[];
    uint32_t pre1[offsetsA];
    uint32_t index;
    uint16_t i, j;
    uint16_t pr, pre2;

    #pragma unroll 16
    for (int i= 0; i <  16; ++i)
        shared_array_sieve[threadIdx.x + (i << 9)] = 0;

    __syncthreads();

    base_index = base_index << 12;

    //precompute
    uint32_t b_idx = blockIdx.x << 12;

    auto pre = [&pre1, &prime_remainders, &index](uint32_t o)
    {
        pre1[o] = prime_remainders[index + o]; // << 3 because we have space for 8 offsets
    };

    auto loop = [&pre1, &pre2, &pr, &index](uint32_t o)
    {
        index = pre1[o] + pre2;
        if(index >= pr)
            index = index - pr;

        for(; index < 262144; index += pr)
        {
            atomicOr(&shared_array_sieve[index >> 5], 1 << (index & 31));
        }
    };

    for (i = nPrimorialEndPrime + threadIdx.x; i < nPrimeLimitA; i += blockDim.x)
    {
        pr = c_primes[i];
        pre2 = blockoffset_mod_p[b_idx + i];

        // precompute
        index = (base_index + i) << 3;

        Unroller<0, offsetsA>::step(pre);
        Unroller<0, offsetsA>::step(loop);
    }

    __syncthreads();
    g_bit_array_sieve += (blockIdx.x << 13);

    #pragma unroll 16
    for (int i = 0; i < 16; ++i) // fixed value
    {
        j = threadIdx.x + (i << 9);
        //atomicOr(&g_bit_array_sieve[j], shared_array_sieve[j]);
        g_bit_array_sieve[j] = shared_array_sieve[j];
    }
}


__global__ void clearsieve_kernel(uint32_t *sieve, uint32_t n_words)
{
    uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;

    if(i < n_words)
        sieve[i] = 0;
}


__global__ void primesieve_kernelB(uint64_t *origins,
                                   uint32_t *bit_array_sieve,
                                   uint32_t bit_array_size,
                                   uint4 *primes,
                                   uint32_t *base_remainders,
                                   uint32_t nPrimorialEndPrime,
                                   uint32_t nPrimeLimit,
                                   uint32_t nOffsets,
                                   uint32_t origin_index)
{
    uint32_t i = nPrimorialEndPrime + blockDim.x * blockIdx.x + threadIdx.x;

    if(i < nPrimeLimit)
    {
        uint4 tmp = primes[i];
        uint64_t origin = origins[origin_index] + base_remainders[i];
        uint64_t recip = make_uint64_t(tmp.z, tmp.w);

        uint32_t index;
        uint32_t mask;

        tmp.z = mod_p_small(origin, tmp.x, recip);

        for(uint32_t o = 0; o < nOffsets; ++o)
        {
            index = tmp.z + c_offsets[c_iA[o]];
            if(index >= tmp.x)
                index -= tmp.x;

            tmp.w = mod_p_small((uint64_t)(tmp.x - index)*tmp.y, tmp.x, recip);


            for(; tmp.w < bit_array_size; tmp.w += tmp.x)
            {
                index = tmp.w >> 5;
                mask = c_mark_mask[tmp.w & 31];

                if((bit_array_sieve[index] & mask) == 0)
                    atomicOr(&bit_array_sieve[index], mask);
            }
        }
    }
}


__global__ void primesieve_kernelC(uint64_t *origins,
                                   uint32_t *bit_array_sieve,
                                   uint32_t bit_array_size,
                                   uint4 *primes,
                                   uint32_t *base_remainders,
                                   uint32_t nPrimorialEndPrime,
                                   uint32_t nPrimeLimit,
                                   uint32_t nOffsets,
                                   uint32_t origin_index)
{
    uint32_t i = nPrimorialEndPrime + blockDim.x * blockIdx.x + threadIdx.x;

    if(i < nPrimeLimit)
    {
        uint4 tmp = primes[i];
        uint64_t origin = origins[origin_index] + base_remainders[i];
        uint64_t recip = make_uint64_t(tmp.z, tmp.w);

        uint32_t index;
        uint32_t mask;

        tmp.z = mod_p_small(origin, tmp.x, recip);

        for(uint32_t o = 0; o < nOffsets; ++o)
        {
            index = tmp.z + c_offsets[c_iA[o]];
            if(index >= tmp.x)
                index -= tmp.x;

            tmp.w = mod_p_small((uint64_t)(tmp.x - index)*tmp.y, tmp.x, recip);


            if(tmp.w < bit_array_size)
            {
                index = tmp.w >> 5;
                mask = c_mark_mask[tmp.w & 31];

                if((bit_array_sieve[index] & mask) == 0)
                    atomicOr(&bit_array_sieve[index], mask);
            }
        }
    }
}


void kernelA0_launch(uint8_t thr_id,
                     uint8_t str_id,
                     uint16_t nPrimeLimitA,
                     uint16_t nOrigins)
{
    uint32_t nThreads = nPrimeLimitA * nOrigins;


    dim3 block(512);
    dim3 grid((nThreads + block.x - 1) / block.x);

    primesieve_kernelA0<<<grid, block, 0, d_Streams[thr_id][str_id]>>>(
        d_origins[thr_id],
        d_primesInverseInvk[thr_id],
        d_prime_remainders[thr_id],
        d_base_remainders[thr_id],
        nOffsetsA,
        nOrigins,
        nThreads);
}

void kernelB0_launch(uint8_t thr_id,
                     uint8_t str_id,
                     uint16_t nPrimeLimitA,
                     uint16_t nOrigins)
{
    uint32_t nThreads = nPrimeLimitA * nOrigins;

    dim3 block(512);
    dim3 grid((nThreads + block.x - 1) / block.x);

    primesieve_kernelB0<<<grid, block, 0, d_Streams[thr_id][str_id]>>>(
        d_origins[thr_id],
        d_primesInverseInvk[thr_id],
        &d_prime_remainders[thr_id][4096 * nOrigins << 3],
        d_base_remainders[thr_id],
        nOffsetsB,
        nOrigins,
        nThreads);
}

extern "C" void cuda_set_origins(uint8_t thr_id, uint32_t nPrimeLimitA, uint64_t *origins, uint32_t nOrigins)
{
    CHECK(cudaMemcpy(d_origins[thr_id], origins, nOrigins * sizeof(uint64_t), cudaMemcpyHostToDevice));


    /* Precompute prime remainders. */

    debug::log(4, FUNCTION, "nPrimeLimitA=", nPrimeLimitA, " nOrigins=", nOrigins);

    for(uint8_t i = 0; i < 4; ++i)
        CHECK(stream_wait_event(thr_id, 0, i, i));

    kernelA0_launch(thr_id, STREAM::SIEVE_A, nPrimeLimitA, nOrigins);
    kernelB0_launch(thr_id, STREAM::SIEVE_B, nPrimeLimitA, nOrigins);

    CHECK(stream_signal_event(thr_id, 0, STREAM::SIEVE_A, EVENT::SIEVE_A));
    CHECK(stream_signal_event(thr_id, 0, STREAM::SIEVE_B, EVENT::SIEVE_B));
}

#define KERNEL_A_LAUNCH(X) primesieve_kernelD_512<X><<<grid, block, sharedSizeBits/8, d_Streams[thr_id][str_id]>>>(\
frameResources[thr_id].d_bit_array_sieve[frame_index], \
d_prime_remainders[thr_id], \
d_blockoffset_mod_p[thr_id], \
origin_index, \
nPrimorialEndPrime, \
nPrimeLimitA)

void kernelA_launch(uint8_t thr_id,
                    uint8_t str_id,
                    uint32_t origin_index,
                    uint8_t frame_index,
                    uint16_t nPrimorialEndPrime,
                    uint16_t nPrimeLimitA,
                    uint32_t nBitArray_Size)
{
    const int sharedSizeBits = 32 * 1024 * 8;
    int nBlocks = (nBitArray_Size + sharedSizeBits-1) / sharedSizeBits;

    dim3 block(512);
    dim3 grid(nBlocks);

    switch(nOffsetsA)
    {
        case 1:  KERNEL_A_LAUNCH(1);  break;
        case 2:  KERNEL_A_LAUNCH(2);  break;
        case 3:  KERNEL_A_LAUNCH(3);  break;
        case 4:  KERNEL_A_LAUNCH(4);  break;
        case 5:  KERNEL_A_LAUNCH(5);  break;
        case 6:  KERNEL_A_LAUNCH(6);  break;
        case 7:  KERNEL_A_LAUNCH(7);  break;
        case 8:  KERNEL_A_LAUNCH(8);  break;
        case 9:  KERNEL_A_LAUNCH(9);  break;
        case 10: KERNEL_A_LAUNCH(10); break;
        case 11: KERNEL_A_LAUNCH(11); break;
        case 12: KERNEL_A_LAUNCH(12); break;
        case 13: KERNEL_A_LAUNCH(13); break;
        case 14: KERNEL_A_LAUNCH(14); break;
        case 15: KERNEL_A_LAUNCH(15); break;
    }
}


#define KERNEL_B_LAUNCH(X)   primesieve_kernelB<<<grid, block, 0, d_Streams[thr_id][str_id]>>>( \
d_origins[thr_id], \
frameResources[thr_id].d_bit_array_sieve[frame_index], \
nBitArray_Size, \
d_primesInverseInvk[thr_id], \
d_base_remainders[thr_id], \
nPrimeLimitA, \
nPrimeLimitB, \
X, \
origin_index )

void kernelB_launch(uint8_t thr_id,
                    uint8_t str_id,
                    uint32_t origin_index,
                    uint8_t frame_index,
                    uint32_t nPrimeLimitA,
                    uint32_t nPrimeLimitB,
                    uint32_t nBitArray_Size)
{
    uint32_t nThreads = nPrimeLimitB - nPrimeLimitA;
    uint32_t nThreadsPerBlock = 128;
    uint32_t nBlocks = (nThreads + nThreadsPerBlock - 1) / nThreadsPerBlock;

    dim3 block(nThreadsPerBlock);
    dim3 grid(nBlocks);

    switch(nOffsetsA)
    {
        case 1: KERNEL_B_LAUNCH(1); break;
        case 2: KERNEL_B_LAUNCH(2); break;
        case 3: KERNEL_B_LAUNCH(3); break;
        case 4: KERNEL_B_LAUNCH(4); break;
        case 5: KERNEL_B_LAUNCH(5); break;
        case 6: KERNEL_B_LAUNCH(6); break;
        case 7: KERNEL_B_LAUNCH(7); break;
        case 8: KERNEL_B_LAUNCH(8); break;
    }
}


#define KERNEL_C_LAUNCH(X)   primesieve_kernelC<<<grid, block, 0, d_Streams[thr_id][str_id]>>>( \
d_origins[thr_id], \
frameResources[thr_id].d_bit_array_sieve[frame_index], \
nBitArray_Size, \
d_primesInverseInvk[thr_id], \
d_base_remainders[thr_id], \
nPrimeLimitB, \
nPrimeLimit, \
X, \
origin_index )

void kernelC_launch(uint8_t thr_id,
                    uint8_t str_id,
                    uint32_t origin_index,
                    uint8_t frame_index,
                    uint32_t nPrimeLimitB,
                    uint32_t nPrimeLimit,
                    uint32_t nBitArray_Size)
{
    uint32_t nThreads = nPrimeLimit - nPrimeLimitB;
    uint32_t nThreadsPerBlock = 128;
    uint32_t nBlocks = (nThreads + nThreadsPerBlock - 1) / nThreadsPerBlock;

    dim3 block(nThreadsPerBlock);
    dim3 grid(nBlocks);

    switch(nOffsetsA)
    {
        case 8: KERNEL_C_LAUNCH(8); break;
        case 7: KERNEL_C_LAUNCH(7); break;
        case 6: KERNEL_C_LAUNCH(6); break;
        case 5: KERNEL_C_LAUNCH(5); break;
        case 4: KERNEL_C_LAUNCH(4); break;
        case 3: KERNEL_C_LAUNCH(3); break;
        case 2: KERNEL_C_LAUNCH(2); break;
        case 1: KERNEL_C_LAUNCH(1); break;






    }
}

void kernel_clear_launch(uint8_t thr_id, uint8_t str_id,
                         uint8_t curr_sieve, uint32_t nBitArray_Size)
{
    uint32_t sharedSizeBits = 32 * 1024 * 8;
    uint32_t allocSize = ((nBitArray_Size*16 + sharedSizeBits-1) / sharedSizeBits) * sharedSizeBits;

    uint32_t nSieveWords = (allocSize + 31) >> 5;

    dim3 block(64);
    dim3 grid((nSieveWords + block.x - 1) / block.x);

    clearsieve_kernel<<<grid, block, 0, d_Streams[thr_id][str_id]>>>(
    frameResources[thr_id].d_bit_array_sieve[curr_sieve], nSieveWords);
}

extern "C" bool cuda_primesieve(uint8_t thr_id,
                                uint64_t primorial,
                                uint16_t nPrimorialEndPrime,
                                uint16_t nPrimeLimitA,
                                uint32_t nPrimeLimitB,
                                uint32_t nPrimeLimit,
                                uint32_t nBitArray_Size,
                                uint32_t nDifficulty,
                                uint32_t sieve_index,
                                uint32_t test_index,
                                uint32_t nOrigins,
                                uint32_t nMaxCandidates)
{
    /* Get the current working sieve and test indices */
    uint8_t prev_sieve = (sieve_index - 1) % FRAME_COUNT;
    uint8_t curr_sieve = sieve_index % FRAME_COUNT;
    uint8_t curr_test = test_index % FRAME_COUNT;
    uint32_t next_test = (test_index + 1) % FRAME_COUNT;
    uint32_t prev_test = (test_index - 1) % FRAME_COUNT;

    /* Make sure current working sieve is finished */
    if(cudaEventQuery(d_Events[thr_id][curr_sieve][EVENT::COMPACT]) == cudaErrorNotReady
    || cudaEventQuery(d_Events[thr_id][prev_test ][EVENT::FERMAT ]) == cudaErrorNotReady)
        return false;
    //CHECK(synchronize_event(thr_id, curr_sieve, EVENT::COMPACT));
    //CHECK(synchronize_event(thr_id, prev_test, EVENT::FERMAT));

    uint8_t nComboThreshold = 8;

    uint32_t origin_index = sieve_index % nOrigins;


    {
        /* Wait for testing and compaction to finish before starting next round. */
        //CHECK(stream_wait_event(thr_id, curr_sieve, STREAM::CLEAR, EVENT::COMPACT));
        //CHECK(stream_wait_event(thr_id, prev_test, STREAM::CLEAR, EVENT::FERMAT));

        /* Clear the current working sieve and signal */
        //kernel_clear_launch(thr_id, STREAM::CLEAR, curr_sieve, nBitArray_Size);


        //CHECK(stream_signal_event(thr_id, curr_sieve, STREAM::CLEAR, EVENT::CLEAR));
    }


    {
        CHECK(stream_wait_event(thr_id, prev_sieve, STREAM::SIEVE_A, EVENT::COMPACT));
        CHECK(stream_wait_event(thr_id, prev_test,  STREAM::SIEVE_A, EVENT::FERMAT));


        /* Single sieve (Launch small sieve, utilizing shared memory and signal) */
        kernelA_launch(thr_id, STREAM::SIEVE_A, origin_index, curr_sieve,
                      nPrimorialEndPrime, nPrimeLimitA, nBitArray_Size);

        CHECK(stream_signal_event(thr_id, curr_sieve, STREAM::SIEVE_A, EVENT::SIEVE_A));
        CHECK(stream_wait_event(thr_id,   curr_sieve, STREAM::SIEVE_B, EVENT::SIEVE_A));

        /* Single sieve (Launch large sieve, utilizing global memory and signal) */
        kernelB_launch(thr_id, STREAM::SIEVE_B, origin_index, curr_sieve,
                      nPrimeLimitA, nPrimeLimitB, nBitArray_Size);

        kernelC_launch(thr_id, STREAM::SIEVE_B, origin_index, curr_sieve,
                      nPrimeLimitB, nPrimeLimit, nBitArray_Size);

        CHECK(stream_signal_event(thr_id, curr_sieve, STREAM::SIEVE_B, EVENT::SIEVE_B));
    }


    {
        CHECK(stream_wait_event(thr_id, curr_sieve, STREAM::SIEVE_A, EVENT::SIEVE_B));


        /* Combo sieve (Launch small sieve, utilizing shared memory and signal) */
        comboA_launch(thr_id, STREAM::SIEVE_A, origin_index, curr_sieve,
                    nPrimorialEndPrime, nPrimeLimitA, nBitArray_Size, nOrigins);

        CHECK(stream_signal_event(thr_id, curr_sieve, STREAM::SIEVE_A, EVENT::SIEVE_A));
        CHECK(stream_wait_event(thr_id,  curr_sieve,  STREAM::SIEVE_B, EVENT::SIEVE_A));

        /* Combo sieve (Launch large sieve, utilizing global memory and signal) */
        comboB_launch(thr_id, STREAM::SIEVE_B, origin_index, curr_sieve,
                      nPrimeLimitA, nPrimeLimitB, nBitArray_Size);

        CHECK(stream_signal_event(thr_id, curr_sieve, STREAM::SIEVE_B, EVENT::SIEVE_B));
    }


    {   /* Launch compaction and signal */
        CHECK(stream_wait_events(thr_id, curr_sieve, STREAM::COMPACT, EVENT::SIEVE_A, EVENT::SIEVE_B));

        kernel_ccompact_launch(thr_id, STREAM::COMPACT, origin_index, nMaxCandidates, curr_sieve, curr_test, next_test, nBitArray_Size, nComboThreshold);

        CHECK(stream_signal_event(thr_id, curr_sieve, STREAM::COMPACT, EVENT::COMPACT));
    }


    debug::log(4, FUNCTION, (uint32_t)thr_id, ": origin index=", sieve_index);

    return true;
}
