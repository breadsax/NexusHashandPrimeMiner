/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_TEMPLATES_BASE_CONNECTION_H
#define NEXUS_LLP_TEMPLATES_BASE_CONNECTION_H

#include <LLP/templates/socket.h>
#include <Util/include/mutex.h>
#include <vector>


namespace LLP
{

    /* Forward declarations. */
    class DDOS_Filter;

    /** BaseConnection
     *
     *  Base Template class to handle outgoing / incoming LLP data for both Client and Server.
     *
     **/
    template <class PacketType>
    class BaseConnection : public Socket
    {
    protected:


        /** Event
         *
         *  Pure Virtual Event Function to be Overridden allowing Custom Read Events.
         *  Each event fired on Header Complete, and each time data is read to fill packet.
         *  Useful to check Header length to maximum size of packet type for DDOS protection,
         *  sending a keep-alive ping while downloading large files, etc.
         *
         *  LENGTH == 0: General Events
         *  LENGTH  > 0 && PACKET: Read nSize Bytes into Data Packet
         *
         **/
        //virtual void Event(uint8_t EVENT, uint32_t LENGTH = 0) = 0;


        /** ProcessPacket
         *
         *  Pure Virtual Process Function. To be overridden with your own custom
         *  packet processing.
         *
         **/
        //virtual bool ProcessPacket() = 0;

    public:

        /** Incoming Packet Being Built. **/
        PacketType     INCOMING;


        /** DDOS Score for Connection. **/
        DDOS_Filter*   DDOS;


        /** Latency in Milliseconds to determine a node's reliability. **/
        std::atomic<uint32_t> nLatency; //milli-seconds


        /** Flag to Determine if DDOS is Enabled. **/
        std::atomic<bool> fDDOS;


        /** Flag to Determine if the connection was made by this Node. **/
        std::atomic<bool> fOUTGOING;


        /** Flag to determine if the connection is active. **/
        std::atomic<bool> fCONNECTED;


        /** Build Base Connection with no parameters **/
        BaseConnection();


        /** Build Base Connection with all Parameters. **/
        BaseConnection(const Socket &SOCKET_IN, DDOS_Filter* DDOS_IN, bool isDDOS = false, bool fOutgoing = false);
        BaseConnection(DDOS_Filter* DDOS_IN, bool isDDOS = false, bool fOutgoing = false);

        /* Default destructor */
        virtual ~BaseConnection();


        /** SetNull
         *
         *  Sets the object to an invalid state.
         *
         **/
        void SetNull();


        /** Connected
         *
         *  Connection flag to determine if socket should be handled if not connected.
         *
         **/
        bool Connected() const;


        /** PacketComplete
         *
         *  Handles two types of packets, requests which are of header >= 128,
         *  and data which are of header < 128.
         *
         **/
        bool PacketComplete() const;


        /** ResetPacket
         *
         *  Used to reset the packet to Null after it has been processed.
         *  This then flags the Connection to read another packet.
         *
         **/
        void ResetPacket();


        /** WritePacket
         *
         *  Write a single packet to the TCP stream.
         *
         *  @param[in] PACKET The packet of type PacketType to write.
         *
         **/
        void WritePacket(const PacketType& PACKET);


        /** ReadPacket
         *
         *  Non-Blocking Packet reader to build a packet from TCP Connection.
         *  This keeps thread from spending too much time for each Connection.
         *
         **/
        virtual void ReadPacket() = 0;


        /** Connect
         *
         *  Connect Socket to a Remote Endpoint.
         *
         *  @param[in] addrConnect The address to connect to.
         *
         *  @return Returns true if successful connection, false otherwise.
         *
         */
        bool Connect(const BaseAddress &addrConnect);



        /** Disconnect
         *
         *  Disconnect Socket. Cleans up memory usage to prevent "memory runs"
         *  from poor memory management.
         *
         **/
        void Disconnect();

    };

}

#endif
