/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_UTIL_INCLUDE_SIGNALS_H
#define NEXUS_UTIL_INCLUDE_SIGNALS_H


#include <condition_variable>

namespace signals
{
        extern std::condition_variable SHUTDOWN;

        /** Shutdown
         *
         *  Shutdown the system and all its subsystems.
         *
         **/
        void Shutdown();


        /** HandleSIGTERM
         *
         *  Catch Signal Handler function
         *
         *  @param[in] signum the signal number
         *
         **/
        void HandleSIGTERM(int signum);


        /** Setup
         *
         *  Setup the signal handlers.
         *
         **/
        void Setup();


        /** Wait
         *
         *  Wait for a signal to occur.
         *
         **/
        void Wait();

}

#endif
