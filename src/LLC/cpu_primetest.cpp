/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLC/include/global.h>
#include <LLC/types/cpu_primetest.h>

#include <TAO/Ledger/types/block.h>

#include <Util/include/runtime.h>
#include <Util/include/debug.h>
#include <Util/include/print_colors.h>
#include <Util/include/prime_config.h>
#include <Util/include/bitmanip.h>
#include <bitset>

#include <iomanip>

namespace LLC
{

    void check_print(uint8_t thr_id, uint64_t nonce, uint32_t sieve_difficulty,
    const char *color, uint8_t chain_length, uint8_t chain_target, uint32_t combo)
    {
        if (chain_length == chain_target)
        {
            std::string devicename = combo ? cuda_devicename(thr_id) : "PrimeTestCPU";

            debug::log(0, "[PRIMES] ",
                color, (int)chain_length, "-Chain Found: ",
                std::fixed, std::setprecision(7), (double)sieve_difficulty / 1e7,
                KNRM,
                "  Nonce: ", std::hex, std::uppercase, std::setfill('0'), std::setw(16), nonce,
                std::dec, " ", devicename, "[", (uint32_t)thr_id, "]");
        }
    }


    PrimeTestCPU::PrimeTestCPU(uint32_t id)
    : Proof(id)
    , zTempVar()
    , zN()
    , zBaseOffsetted()
    , zBaseOrigin()
    , zPrimeOrigin()
    , zResidue()
    , zPrimorialMod()
    , work()
    , mapTest()
    , gpu_begin(32)
    , gpu_end(0)
    {
    }


    PrimeTestCPU::~PrimeTestCPU()
    {
    }


    void PrimeTestCPU::Load()
    {
        debug::log(3, FUNCTION, "PrimeTestCPU", static_cast<uint32_t>(nID));

        mpz_init(zTempVar);
        mpz_init(zBaseOffsetted);
        mpz_init(zN);
        mpz_init(zBaseOrigin);
        mpz_init(zPrimeOrigin);
        mpz_init(zResidue);
        mpz_init(zPrimorialMod);

        /* Store a map of false positives for GPU Fermat offset tests. */
        for(uint8_t i = 0; i < vOffsetsT.size(); ++i)
            mapTest[vOffsetsT[i]] = i;

        /* Find the begin and end offsets for gpu sieving */
        for(uint8_t i = 0; i < vOffsetsA.size(); ++i)
        {
            gpu_begin = std::min(gpu_begin, vOffsetsA[i]);
            gpu_end = std::max(gpu_end, vOffsetsA[i]);
        }
        for(uint8_t i = 0; i < vOffsetsB.size(); ++i)
        {
            gpu_begin = std::min(gpu_begin, vOffsetsB[i]);
            gpu_end = std::max(gpu_end, vOffsetsB[i]);
        }

    }


    void PrimeTestCPU::Init()
    {
        debug::log(3, FUNCTION, "PrimeTestCPU", static_cast<uint32_t>(nID));

        /* Atomic set reset flag to false. */
        fReset = false;
    }


    bool PrimeTestCPU::Work()
    {
        bool have_work = false;

        if(fReset.load())
            return false;


        /* Check the work queue for nonce results from GPU. */
        {
            std::unique_lock<std::mutex> lk(g_work_mutex);
            if (!g_work_queue.empty())
            {
                work = g_work_queue.front();
                g_work_queue.pop_front();

                lk.unlock();
                have_work = true;
            }
        }

        /* Return (and wait a little) if we don't have work. */
        if (!have_work || fReset.load())
            return false;

        /* Set the block. */
        block = work.block;

        uint1024_t nChainEnd = 0;
        uint1024_t nPrimeOrigin = block.ProofHash();
        uint64_t nNonce = 0;
        uint64_t offset = 0;
        uint32_t combo = 0;
        uint32_t nHeight = block.nHeight;
        uint32_t nWorkCount = (uint32_t)work.nonce_offsets.size();
        uint32_t nPrimeDifficulty = 0;
        uint32_t nPrimeDifficulty2 = 0;

        /* Get the begin and end offsets of the chain. */
        uint32_t chain_offset_beg = 0;
        uint32_t chain_offset_end = 0;

        /* Get the number of survived offsets. */
        uint32_t chain_length = 0;


        uint32_t i = 0;
        uint8_t j = 0;
        uint8_t nOffsets = vOffsets.size();
        uint8_t nPrimeGap = 0;

        /* Get the prime origin from the block and import into GMP. */
        mpz_import(zPrimeOrigin, 32, -1, sizeof(uint32_t), 0, 0, nPrimeOrigin.begin());

        /* Compute the primorial mod from the origin. */
        mpz_mod(zPrimorialMod, zPrimeOrigin, zPrimorial);
        mpz_sub(zPrimorialMod, zPrimorial, zPrimorialMod);
        mpz_add(zBaseOrigin, zPrimeOrigin, zPrimorialMod);


        /* Log message. */
        debug::log(3, "PrimeTestCPU[", (uint32_t)nID, "]: ", nWorkCount, " nonces from ", (uint32_t)work.thr_id);

        /* Process each result from array of nonces. */
        for(i = 0; i < nWorkCount; ++i)
        {
            if(fReset.load() || nHeight != block.nHeight)
                return false;

            /* Obtain work nonce offset and nonce meta. */
            offset = work.nonce_offsets[i];
            combo = work.nonce_meta[i];

            /* Compute the base offset of the nonce */
            mpz_add_ui(zBaseOffsetted, zBaseOrigin, offset);

            if(combo)
            {
                /* Mask off high and low 1-bits not set by gpu sieve */
                combo = (combo >> gpu_begin) << gpu_begin;
                combo = (combo << (32 - gpu_end)) >> (32 - gpu_end);
                //debug::log(0, " gpu combo=", std::bitset<32>(combo));

                /* Loop through combo and test remaining offsets. */
                for(j = 0; j < nOffsets; ++j)
                {
                    /* Skip GPU offsets. Already Fermat tested. */
                    if(mapTest.find(j) != mapTest.end())
                        continue;

                    /* Don't test failed sieved offsets. */
                    if(combo & (1 << j))
                        continue;

                    /* Start fresh from each offset remaining. */
                    mpz_add_ui(zTempVar, zBaseOffsetted, vOffsets[j]);

                    /* Check for Fermat test. */
                    mpz_sub_ui(zN, zTempVar, 1);
                    mpz_powm(zResidue, zTwo, zN, zTempVar);
                    if (mpz_cmp_ui(zResidue, 1) == 0)
                        ++PrimesFound[j];
                    else
                        combo |= (1 << j);


                    ++PrimesChecked[j];
                    ++Tests_CPU;
                }



                /* Invert the bits and mask off the high bits. */
                combo = (~combo) & (0xFFFFFFFF >> nOffsets);

                /* If there are no more combo bits, this candidate will never lead to a solution. */
                if(combo == 0)
                    continue;

                /* Get the begin and end offsets of the chain. */
                chain_offset_beg = convert::ctz(combo);
                chain_offset_end = nOffsets - convert::clz(combo << (32 - 1 - nOffsets));

                /* Get the number of survived offsets. */
                chain_length = convert::popc(combo);
            }
            else
            {
                chain_offset_beg = 0;
                chain_offset_end = 0;
                chain_length = 1;
            }

            //if(chain_length >= 3)
            //    debug::log(0, "combo=", std::bitset<32>(combo), " len=", chain_length, " beg=", chain_offset_beg,
            //               " end=", chain_offset_end);


            if(chain_offset_beg > chain_offset_end)
                return debug::error("Offset begin greater than end.",
                " beg=", chain_offset_beg,
                " end=", chain_offset_end);

            if(chain_offset_beg >= nOffsets || chain_offset_end >= nOffsets)
            {
                return debug::error("Offset index out of bounds.",
                " beg=", chain_offset_beg,
                " end=", chain_offset_end,
                " combo=", std::bitset<32>(combo));
            }


            chain_offset_beg = vOffsets[chain_offset_beg];
            chain_offset_end = vOffsets[chain_offset_end] + 2;


            /* Search for primes after small cluster */
            nPrimeGap = 0;
            mpz_add_ui(zTempVar, zBaseOffsetted, chain_offset_end);
            while (nPrimeGap <= 12)
            {
                if(fReset.load() || nHeight != block.nHeight)
                    return false;


                mpz_sub_ui(zN, zTempVar, 1);
                mpz_powm(zResidue, zTwo, zN, zTempVar);
                if (mpz_cmp_ui(zResidue, 1) == 0)
                {
                    //++PrimesFound;
                    ++chain_length;

                    nPrimeGap = 0;
                }

                //++PrimesChecked;
                ++Tests_CPU;

                mpz_add_ui(zTempVar, zTempVar, 2);
                chain_offset_end += 2;
                nPrimeGap += 2;
            }


            /* Translate nonce offset of begin prime to global offset. */
            mpz_add_ui(zTempVar, zBaseOffsetted, chain_offset_beg);
            mpz_sub(zTempVar, zTempVar, zPrimeOrigin);
            nNonce = mpz_get_ui(zTempVar);

            if(fReset.load() || nHeight != block.nHeight)
                return false;


            if (chain_length >= 3)
            {
                nChainEnd = nPrimeOrigin + nNonce + chain_offset_end;

                nPrimeDifficulty = SetBits(GetPrimeDifficulty(nChainEnd, chain_length));

                nPrimeDifficulty2 = GetPrimeBits(CBigNum(nPrimeOrigin + nNonce));

                if(nPrimeDifficulty != nPrimeDifficulty2)
                {
                    //debug::error("Mismatch, GMP: ", nPrimeDifficulty, "  OpenSSL: ", nPrimeDifficulty2);
                    nPrimeDifficulty = nPrimeDifficulty2;
                    chain_length = (uint32_t)(nPrimeDifficulty2 / 1e7);
                }

                /* Compute the weight for WPS. */
                if(chain_length >= 3)
                    nWeight += nPrimeDifficulty * 50;


                if (nPrimeDifficulty > nLargest)
                    nLargest = nPrimeDifficulty;

                if(chain_length < MAX_CHAIN_LENGTH)
                    ++nChainCounts[chain_length];
                else
                {
                    debug::error("Chain length of ", chain_length, " too high. Max ", MAX_CHAIN_LENGTH - 1);
                    continue;
                }

                /* Check print messages. */
                if(chain_length >= 5)
                {
                    check_print(work.thr_id, nNonce, nPrimeDifficulty, KLGRN, chain_length, 5, combo);
                    check_print(work.thr_id, nNonce, nPrimeDifficulty, KLCYN, chain_length, 6, combo);
                    check_print(work.thr_id, nNonce, nPrimeDifficulty, KLMAG, chain_length, 7, combo);
                    check_print(work.thr_id, nNonce, nPrimeDifficulty, KLYEL, chain_length, 8, combo);
                    check_print(work.thr_id, nNonce, nPrimeDifficulty, KLYEL, chain_length, 9, combo);
                }


                /* Check Difficulty */
                if (nPrimeDifficulty >= block.nBits && !fReset.load() && nHeight == block.nHeight)
                {
                    debug::log(0, "[MASTER] Found Prime Block ", block.ProofHash().ToString().substr(0, 20), " with Difficulty ", std::fixed, std::setprecision(7), nPrimeDifficulty/1e7);

                    /* Set the block nonce and return. */
                    block.nNonce = nNonce;
                    fReset = true;
                    return true;
                }
            }

        }

        return false;
    }


    void PrimeTestCPU::Shutdown()
    {
        debug::log(3, FUNCTION, "PrimeTestCPU", static_cast<uint32_t>(nID));

        /* Atomic set reset flag to true. */
        fReset = true;

        mpz_clear(zTempVar);
        mpz_clear(zN);
        mpz_clear(zBaseOffsetted);
        mpz_clear(zBaseOrigin);
        mpz_clear(zPrimeOrigin);
        mpz_clear(zResidue);
        mpz_clear(zPrimorialMod);
    }

}
