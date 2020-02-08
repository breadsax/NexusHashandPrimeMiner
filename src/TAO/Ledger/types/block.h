/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_TAO_LEDGER_TYPES_BLOCK_H
#define NEXUS_TAO_LEDGER_TYPES_BLOCK_H

#include <LLC/types/uint1024.h>

//forward declerations for BigNum
namespace LLC
{
    class CBigNum;
}

/* Global TAO namespace. */
namespace TAO
{

    /* Ledger Layer namespace. */
    namespace Ledger
    {

        /** Block
         *
         *  Nodes collect new transactions into a block, hash them into a hash tree,
         *  and scan through nonce values to make the block's hash satisfy validation
         *  requirements.
         *
         */
        class Block
        {
        public:

            /** The blocks version for. Useful for changing rules. **/
            uint32_t nVersion;


            /** The previous blocks hash. Used to chain blocks together. **/
            uint1024_t hashPrevBlock;


            /** The Merkle Root. A merkle tree of transaction hashes included in header. **/
            uint512_t hashMerkleRoot;


            /** The Block Channel. This number designates what validation algorithm is required. **/
            uint32_t nChannel;


            /** The Block's Height. This number tells what block number this is in the chain. **/
            uint32_t nHeight;


            /** The Block's Bits. This number is a compact representation of the required difficulty. **/
            uint32_t nBits;


            /** The Block's nOnce. This number is used to find the "winning" hash. **/
            uint64_t nNonce;


            /** The Block's timestamp. This number is locked into the signature hash. **/
            uint32_t nTime; //TODO: make this 64 bit


            /** The bytes holding the blocks signature. Signed by the block creator before broadcast. **/
            std::vector<uint8_t> vchBlockSig;



            /** The default constructor. Sets block state to Null. **/
            Block();


            /** A base constructor.
             *
             *  @param[in] nVersionIn The version to set block to
             *  @param[in] hashPrevBlockIn The previous block being linked to
             *  @param[in] nChannelIn The channel this block is being created for
             *  @param[in] nHeightIn The height this block is being created at.
             *
            **/
            Block(uint32_t nVersionIn, uint1024_t hashPrevBlockIn, uint32_t nChannelIn, uint32_t nHeightIn);


            /** Copy constructor. **/
            Block(const Block& block);


            /** Default Destructor **/
            virtual ~Block();


            /** SetNull
             *
             *  Set the block to Null state.
             *
             **/
            virtual void SetNull();


            /** SetChannel
             *
             *  Sets the channel for the block.
             *
             *  @param[in] nNewChannel The channel to set.
             *
             **/
            void SetChannel(uint32_t nNewChannel);


            /** GetChannel
             *
             *  Gets the channel the block belongs to.
             *
             *  @return The channel assigned. (uint32_t)
             *
             */
            uint32_t GetChannel() const;


            /** IsNull
             *
             *  Checks the Null state of the block.
             *
             *  @return True if null, false otherwise.
             *
             **/
            bool IsNull() const;


            /** GetBlockTime
             *
             *  Returns the current UNIX timestamp of the block.
             *
             *  @return 64-bit uint32_teger of timestamp.
             *
             **/
            uint64_t GetBlockTime() const;


            /** GetPrime
             *
             *  Get the Prime number for the block (hash + nNonce).
             *
             *  @return Prime number stored as a CBigNum. (wrapper for BIGNUM in OpenSSL)
             *
             **/
            LLC::CBigNum GetPrime() const;


            /** ProofHash
             *
             *  Get the Proof Hash of the block. Used to verify work claims.
             *
             *  @return 1024-bit proof hash
             *
             **/
            uint1024_t ProofHash() const;


            /** SignatureHash
             *
             *  Get the Signature Hash of the block. Used to verify work claims.
             *
             *  @return 1024-bit signature hash
             *
             **/
            uint1024_t SignatureHash() const;


            /** GetHash
             *
             *  Get the Hash of the block.
             *
             *  @return 1024-bit block hash
             *
             **/
            uint1024_t GetHash() const;


            /** UpdateTime
             *
             *  Update the blocks timestamp
             *
             **/
            void UpdateTime();


            /** IsProofOfStake
             *
             *  @return True if the block is proof of stake, false otherwise.
             *
             **/
            bool IsProofOfStake() const;


            /** IsProofOfWork
             *
             *  @return True if the block is proof of work, false otherwise.
             *
             **/
            bool IsProofOfWork() const;


            /** BuildMerkleTree
             *
             *  Build the merkle tree from the transaction list.
             *
             *  @return The 512-bit merkle root
             *
             **/
            uint512_t BuildMerkleTree(std::vector<uint512_t> vMerkleTree) const;


            /** print
             *
             *  Dump to the log file the raw block data
             *
             **/
            void print() const;


            /** VerifyWork
             *
             *  Verify the work was completed by miners as advertised.
             *
             *  @return True if work is valid, false otherwise.
             *
             **/
            bool VerifyWork() const;


            /** Serialize
             *
             *  Convert the Header of a Block into a Byte Stream for
             *  Reading and Writing Across Sockets.
             *
             *  @return Returns a vector of serialized byte information.
             *
             **/
            std::vector<uint8_t> Serialize() const;


            /** Deserialize
             *
             *  Convert Byte Stream into Block Header.
             *
             *  @param[in] vData The byte stream containing block info.
             *
             **/
            void Deserialize(const std::vector<uint8_t>& vData);

        };
    }
}

#endif
