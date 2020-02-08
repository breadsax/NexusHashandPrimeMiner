/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLC/include/global.h>
#include <LLC/types/cpu_primesieve.h>

#include <TAO/Ledger/types/block.h>

#include <Util/include/runtime.h>
#include <Util/include/debug.h>
#include <Util/include/print_colors.h>
#include <Util/include/prime_config.h>

#include <iomanip>
#include <cstring> //memset

namespace LLC
{
    PrimeSieveCPU::PrimeSieveCPU(uint32_t id)
    : Proof(id)
    , vWorkOrigins()
    , vBaseRemainders()
    , nSieveBits(1 << 23)
    , nSievePrimes(1 << 23)
    , pBitArraySieve(nullptr)
    , nSieveIndex(0)
    , nBitArrayIndex(0)
    , nSievesPerOriginCPU(5)
    , zPrimeOrigin()
    , zBaseOrigin()
    , zPrimorialMod()
    , zTempVar()
    , zFirstSieveElement()
    , zN()
    , zResidue()
    {
    }


    PrimeSieveCPU::~PrimeSieveCPU()
    {
    }


    void PrimeSieveCPU::Load()
    {
        debug::log(3, FUNCTION, "PrimeSieveCPU", static_cast<uint32_t>(nID));

        /* Initialize the GMP objects. */
        mpz_init(zPrimeOrigin);
        mpz_init(zBaseOrigin);
        mpz_init(zPrimorialMod);
        mpz_init(zTempVar);
        mpz_init(zFirstSieveElement);
        mpz_init(zN);
        mpz_init(zResidue);

        /* Create the bit array sieve. */
        pBitArraySieve = (uint32_t *)malloc(nSieveBits >> 3);

        /* Create empty initial base remainders. */
        vBaseRemainders.assign(nSievePrimes, 0);
    }


    void PrimeSieveCPU::Init()
    {
        debug::log(3, FUNCTION, "PrimeSieveCPU", static_cast<uint32_t>(nID));

        /* Atomic set reset flag to false. */
        fReset = false;
        nSieveIndex = 0;
        nBitArrayIndex = 0;
        vWorkOrigins = vOrigins;

        /* Set the prime origin from the block hash. */
        uint1024_t nPrimeOrigin = block.ProofHash();
        mpz_import(zPrimeOrigin, 32, -1, sizeof(uint32_t), 0, 0, nPrimeOrigin.begin());


        /* Compute the primorial mod from the origin. */
        mpz_mod(zPrimorialMod, zPrimeOrigin, zPrimorial);
        mpz_sub(zPrimorialMod, zPrimorial, zPrimorialMod);
        mpz_add(zBaseOrigin, zPrimeOrigin, zPrimorialMod);
    }


    bool PrimeSieveCPU::Work()
    {
        uint32_t nOrigins = vWorkOrigins.size();

        /* Check for early out. */
        if(fReset.load())
            return false;

        /* Get the current origin index. */
        uint32_t nOriginIndex = nSieveIndex % nOrigins;

        /* Compute first sieving element. */
        mpz_add_ui(zFirstSieveElement, zBaseOrigin, vWorkOrigins[nOriginIndex]);

        /* Compute the base remainders. */
        for(uint32_t i = nPrimorialEndPrime; i < nSievePrimes; ++i)
        {
            /* Get the global sieving prime. */
            uint32_t p   = primesInverseInvk[i * 4];
            vBaseRemainders[i] = mpz_tdiv_ui(zFirstSieveElement, p);
        }


        /* sieve the bit array */
        cpu_sieve();

        /* Add nonce offsets to queue. */
        cpu_compact(vWorkOrigins[nOriginIndex]);

        bool fNewSieve = nOriginIndex == 0;
        bool fLastSieve = nBitArrayIndex == nSievesPerOriginCPU - 1;

        /* Determine if we should synchronize early due to running out of work. */
        bool fSynchronize = fNewSieve && fLastSieve;


        /* Increment the sieve index. */
        ++nSieveIndex;
        SievedBits += nSieveBits;


        if(fNewSieve)
            ++nBitArrayIndex;

        /* Shift the working origin over by an entire sieve range. */
        vWorkOrigins[nOriginIndex] += nPrimorial * nSieveBits;

        if(fSynchronize)
        {
            debug::log(0, FUNCTION, (uint32_t)nID, " - Requesting more work");
            fReset = true;
        }

        return false;
    }


    void PrimeSieveCPU::Shutdown()
    {
        debug::log(3, FUNCTION, "PrimeSieveCPU", static_cast<uint32_t>(nID));

        /* Atomic set reset flag to true. */
        fReset = true;

        /* Free the GMP object memory. */
        mpz_clear(zPrimeOrigin);
        mpz_clear(zBaseOrigin);
        mpz_clear(zPrimorialMod);
        mpz_clear(zTempVar);
        mpz_clear(zFirstSieveElement);
        mpz_clear(zN);
        mpz_clear(zResidue);


        /* Free the bit array sieve memory. */
        free(pBitArraySieve);
    }


    void PrimeSieveCPU::cpu_sieve()
    {
        /* Clear the bit array. */
        memset(pBitArraySieve, 0x00, nSieveBits >> 3);

        /* Loop through and sieve with each sieving offset. */
        for(uint32_t o = 0; o < vOffsetsA.size(); ++o)
        {
            /* Loop through each sieving prime and sieve. */
            for(uint32_t i = nPrimorialEndPrime; i < nSievePrimes && !fReset.load(); ++i)
                sieve_offset(i, o);
        }
    }


    void PrimeSieveCPU::cpu_compact(uint64_t base_offset)
    {
        std::vector<uint64_t> vNonces;
        std::vector<uint32_t> vMeta;

        uint64_t nonce;

        for(uint32_t i = 0; i < nSieveBits && !fReset.load(); ++i)
        {
            /* Make sure this offset survived the sieve. */
            if(pBitArraySieve[i >> 5] & (1 << (i & 31)))
                continue;

            nonce = base_offset + (uint64_t)i * nPrimorial;

            /* If there was at least one prime found, add it to the list. */
            if(cpu_pretest(nonce, 0))
            {
                ++PrimesFound[0];
                vNonces.push_back(nonce);
                vMeta.push_back(0);
            }

            ++PrimesChecked[0];
            ++Tests_CPU;
        }

        if(vNonces.size())
        {
            /* Atomic add nonces to work queue for testing. */
            std::unique_lock<std::mutex> lk(g_work_mutex);
            g_work_queue.emplace_back(work_info(vNonces, vMeta, block, nID));
        }
    }


    bool PrimeSieveCPU::cpu_pretest(uint64_t nonce, uint32_t o)
    {
        mpz_add_ui(zTempVar, zBaseOrigin, nonce);
        mpz_add_ui(zTempVar, zTempVar, vOffsets[o]);

        /*Check for Fermat test. */
        mpz_sub_ui(zN, zTempVar, 1);
        mpz_powm(zResidue, zTwo, zN, zTempVar);
        if(mpz_cmp_ui(zResidue, 1) == 0)
            return true;

        return false;
    }


    void PrimeSieveCPU::sieve_offset(uint32_t i, uint32_t o)
    {
        /* index into primesInverseInvk */
        uint32_t idx = i * 4;

        /* Get the global prime and inverse. */
        uint32_t p   = primesInverseInvk[idx];
        uint32_t inv = primesInverseInvk[idx + 1];


        /* Compute remainder. */
        uint32_t remainder = vBaseRemainders[i] + vOffsets[vOffsetsA[o]];

        if(p < remainder)
            remainder -= p;

        uint64_t r = (uint64_t)(p - remainder) * (uint64_t)inv;

        /* Compute the starting sieve array index. */
        uint32_t index = r % p;

        /* Sieve. */
        while (index < nSieveBits)
        {
            pBitArraySieve[index >> 5] |= (1 << (index & 31));
            index += p;
        }
    }




}
