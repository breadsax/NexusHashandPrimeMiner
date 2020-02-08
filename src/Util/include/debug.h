/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_UTIL_INCLUDE_DEBUG_H
#define NEXUS_UTIL_INCLUDE_DEBUG_H

#include <string>
#include <cstdint>
#include <iosfwd>
#include <sstream>
#include <fstream>
#include <iomanip>

#include <Util/include/args.h>
#include <Util/include/config.h>
#include <Util/include/runtime.h>
#include <Util/include/mutex.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define ANSI_COLOR_BRIGHT_RED     "\u001b[31;1m"
#define ANSI_COLOR_BRIGHT_GREEN   "\u001b[32;1m"
#define ANSI_COLOR_BRIGHT_YELLOW  "\u001b[33;1m"
#define ANSI_COLOR_BRIGHT_BLUE    "\u001b[34;1m"
#define ANSI_COLOR_BRIGHT_MAGENTA "\u001b[35;1m"
#define ANSI_COLOR_BRIGHT_CYAN    "\u001b[36;1m"
#define ANSI_COLOR_BRIGHT_WHITE   "\u001b[37;1m"

#define ANSI_COLOR_FUNCTION "\u001b[1m"

#define VALUE(data) data
//#define FUNCTION ANSI_COLOR_FUNCTION "%s" ANSI_COLOR_RESET " : "

#define NODE ANSI_COLOR_FUNCTION "Node" ANSI_COLOR_RESET " : ", "\u001b[1m", GetAddress().ToStringIP(), ANSI_COLOR_RESET, " "

/* Support for Windows */
#ifndef __PRETTY_FUNCTION__
#define __PRETTY_FUNCTION__ __FUNCTION__
#endif

#define FUNCTION ANSI_COLOR_FUNCTION, __PRETTY_FUNCTION__, ANSI_COLOR_RESET, " : "

namespace debug
{

    extern std::mutex DEBUG_MUTEX;
    extern std::ofstream ssFile;
    extern thread_local std::string strLastError;

    /** Block debug output flags. **/
    struct flags
    {
        enum
        {
            header        = (1 << 0),
            tx            = (1 << 1),
            chain         = (1 << 2)
        };
    };


    /** Initialize
     *
     *  Write startup information into the log file.
     *
     **/
    void Initialize();


    /** Shutdown
     *
     *  Close the debug log file.
     *
     **/
    void Shutdown();


    /** print_args
     *
     *  Overload for varadaic templates.
     *
     *  @param[out] s The stream being written to.
     *  @param[in] head The object being written to stream.
     *
     **/
    template<class Head>
    void print_args(std::ostream& s, Head&& head)
    {
        s << std::forward<Head>(head);
    }


    /** print_args
     *
     *  Handle for variadic template pack
     *
     *  @param[out] s The stream being written to.
     *  @param[in] head The object being written to stream.
     *  @param[in] tail The variadic parameters.
     *
     **/
    template<class Head, class... Tail>
    void print_args(std::ostream& s, Head&& head, Tail&&... tail)
    {
        s << std::forward<Head>(head);
        print_args(s, std::forward<Tail>(tail)...);
    }


    /** safe_printstr
     *
     *  Safe handle for writing objects into a string.
     *
     *  @param[out] s The stream being written to.
     *  @param[in] head The object being written to stream.
     *  @param[in] tail The variadic parameters.
     *
     **/
    template<class... Args>
    std::string safe_printstr(Args&&... args)
    {
        std::ostringstream ss;
        print_args(ss, std::forward<Args>(args)...);

        return ss.str();
    }


    /** log_
     *
     *  Writes log output to console and debug file with timestamps.
     *  Encapsulated log for improved compile time. Not thread safe.
     *
     **/
     void log_(time_t &timestamp, std::string &debug_str);


    /** log
     *
     *  Safe constant format debugging logs.
     *  Dumps to console or to log file.
     *
     *  @param[in] nLevel The log level being written.
     *  @param[in] args The variadic template arguments in.
     *
     **/
    template<class... Args>
    void log(uint32_t nLevel, Args&&... args)
    {
        /* Don't write if log level is below set level. */
        if(config::GetArg("-verbose", 0) < nLevel)
            return;

        /* Lock the mutex. */
        LOCK(DEBUG_MUTEX);

        /* Get the debug string and log file. */
        std::string debug = safe_printstr(args...);

        /* Get the timestamp. */
        time_t timestamp = std::time(nullptr);

        log_(timestamp, debug);
    }


    /** error
     *
     *  Safe constant format debugging error logs.
     *  Dumps to console or to log file.
     *
     *  @param[in] args The variadic template arguments in.
     *
     *  @return Returns false always. (Assumed to return an error.)
     *
     **/
    template<class... Args>
    bool error(Args&&... args)
    {
        strLastError = safe_printstr(args...);

        log(0, ANSI_COLOR_BRIGHT_RED, "ERROR: ", ANSI_COLOR_RESET, args...);



        return false;
    }


    /** success
     *
     *  Safe constant format debugging success logs.
     *  Dumps to console or to log file.
     *
     *  @param[in] args The variadic template arguments in.
     *
     *  @return Returns true always. (Assumed to return successful.)
     *
     **/
    template<class... Args>
    bool success(Args&&... args)
    {
        log(0, ANSI_COLOR_BRIGHT_GREEN, "SUCCESS: ", ANSI_COLOR_RESET, args...);

        return true;
    }


    /** rfc1123Time
     *
     *  Special Specification for HTTP Protocol.
     *  TODO: This could be cleaned up I'd say.
     *
     **/
    std::string rfc1123Time();


    /** GetLastError
     *
     *  Gets the last error string logged via debug::error and clears the last error
     *
     *  @return The last error string logged via debug::error
     *
     **/
    std::string GetLastError();


    /** check_log_archive
     *
     *  Checks if the current debug log should be closed and archived. This
     *  function will close the current file if the max file size is exceeded,
     *  rename it, and open a new file. It will delete the oldest file if it
     *  exceeds the max number of files.
     *
     *  @param[in] outFile The output file stream used to update debug files
     *
     **/
    void check_log_archive(std::ofstream &outFile);


    /** debug_filecount
     *
     *  Returns the number of debug files present in the debug directory.
     *
     **/
    uint32_t debug_filecount();


    /** log_path
     *
     *  Builds an indexed debug log path for a file.
     *
     *  @param[in] nIndex The index for the debug log path.
     *
     *  @return Returns the absolute path to the log file.
     *
     **/
    std::string log_path(uint32_t nIndex);
}
#endif
