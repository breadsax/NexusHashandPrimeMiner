/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#ifdef WIN32 //TODO: use GetFullPathNameW in system_complete if getcwd not supported

#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cstdio> //remove(), rename()
#include <cerrno>
#include <cstring>
#include <iostream>
#include <fstream>

#include <Util/include/debug.h>
#include <Util/include/filesystem.h>
#include <Util/include/mutex.h>

#include <sys/stat.h>

#ifdef WIN32
#include <shlwapi.h>
#include <direct.h>

/* Set up defs properly before including windows.h */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600    //minimum Windows Vista version for winsock2, etc.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1  //prevents windows.h from including winsock.h and messing with winsock2.h definitions we use
#endif

#ifndef NOMINMAX
#define NOMINMAX //prevents windows.h from including min/max and potentially interfering with std::min/std::max
#endif

#include <windows.h>
#endif

namespace filesystem
{
    /* Mutex to lock FILESYSTEM and prevent race conditions. */
    std::mutex FILESYSTEM_MUTEX;

    /* Removes a directory from the specified path. */
    bool remove_directories(const std::string& path)
    {
        if(!exists(path))
            return false;

        LOCK(FILESYSTEM_MUTEX);

        #ifdef WIN32
        if(_rmdir(debug::safe_printstr("rm -rf ", path).c_str()) != -1) //TODO: @scottsimon36 test this for windoze
            return true;
        #else
        if(system(debug::safe_printstr("rm -rf ", path).c_str()) == 0)
            return true;
        #endif

        return false;
    }


    /* Removes a file or folder from the specified path. */
    bool remove(const std::string& path)
    {
        if(!exists(path))
            return false;

        LOCK(FILESYSTEM_MUTEX);

        if(std::remove(path.c_str()) != 0)
            return false;

        return true;
    }


    /*  Renames a file or folder from the specified old path to a new path. */
    bool rename(const std::string &pathOld, const std::string &pathNew)
    {
        if(!exists(pathOld))
            return false;

        LOCK(FILESYSTEM_MUTEX);

        if(std::rename(pathOld.c_str(), pathNew.c_str()) != 0)
            return false;

        return true;
    }


    /* Determines if the file or folder from the specified path exists. */
    bool exists(const std::string &path)
    {
        LOCK(FILESYSTEM_MUTEX);

        struct stat statbuf;

        return stat(path.c_str(), &statbuf) == 0;
    }


    /* Copy a file. */
    bool copy_file(const std::string &pathSource, const std::string &pathDest)
    {
        try
        {
            /* Make sure destination is a file, not a directory. */
            if(exists(pathDest) && is_directory(pathDest))
                return false;

            /* If destination file exists, remove it (ie, we overwrite the file) */
            if(exists(pathDest))
                filesystem::remove(pathDest);

            LOCK(FILESYSTEM_MUTEX);

            /* Get the input stream of source file. */
            std::ifstream sourceFile(pathSource, std::ios::binary);
            sourceFile.exceptions(std::ios::badbit);

            /* Get the output stream of destination file. */
            std::ofstream destFile(pathDest, std::ios::binary);
            destFile.exceptions(std::ios::badbit);

            /* Copy the bytes. */
            destFile << sourceFile.rdbuf();

            /* Close the file handles and flush buffers. */
            sourceFile.close();
            destFile.close();

#ifndef WIN32
            /* Set destination file permissions (read/write by owner only for data files) */
            mode_t m = S_IRUSR | S_IWUSR;
            int file_des = open(pathDest.c_str(), O_RDWR);

            if(file_des < 0)
                return false;

            fchmod(file_des, m);
#endif

        }
        catch(const std::ios_base::failure &e)
        {
            return debug::error(FUNCTION, " failed to write ", e.what());
        }

        return true;
    }

    /* Determines if the specified path is a folder. */
    bool is_directory(const std::string &path)
    {
        LOCK(FILESYSTEM_MUTEX);

        struct stat statbuf;

        if(stat(path.c_str(), &statbuf) != 0)
            return false;

        if(S_ISDIR(statbuf.st_mode))
            return true;

        return false;
    }


    /*  Recursively create directories along the path if they don't exist. */
    bool create_directories(const std::string &path)
    {
        /* Start loop at 1. Allows for root (Linux) or relative path (Windows) separator at 0 */
        for(uint32_t i = 1; i < path.length(); i++)
        {
            bool isSeparator = false;
            const char& currentChar = path.at(i);

        #ifdef WIN32
            /* For Windows, support both / and \ directory separators.
             * Ignore separator after drive designation, as in C:\
             */
            if((currentChar == '/' || currentChar == '\\')  && path.at(i-1) != ':')
                isSeparator = true;
        #else
            if(currentChar == '/')
                isSeparator = true;
        #endif

            if(isSeparator)
            {
                std::string createPath(path, 0, i);
                if(!create_directory(createPath))
                    return false;
            }
        }

        return true;
    }


    /* Create a single directory at the path if it doesn't already exist. */
    bool create_directory(const std::string &path)
    {
        if(exists(path)) //if the directory exists, don't attempt to create it
            return true;

        LOCK(FILESYSTEM_MUTEX);

        /* Set directory with read/write/search permissions for owner/group/other */
    #ifdef WIN32
        int status = mkdir(path.c_str());
    #else
        mode_t m = S_IRWXU | S_IRWXG | S_IRWXO;
        int status = mkdir(path.c_str(), m);
    #endif

        /* Handle failures. */
        if(status < 0)
        {
            return debug::error(FUNCTION, "Failed to create directory: ", path, "\nReason: ", strerror(errno));
        }

        return true;
    }


    /* Obtain the system complete path from a given relative path. */
    std::string system_complete(const std::string &path)
    {
        char buffer[MAX_PATH] = {0};

        std::string fullPath;

        LOCK(FILESYSTEM_MUTEX);

    #ifdef WIN32

        /* Use Windows API for path generation. */
        if(!PathIsRelativeA(path.c_str()))
        {
            /* Path is absolute */
            fullPath = path;
        }
        else
        {
            /* Path is relative. */
            char *pTemp = nullptr;
            GetFullPathNameA(path.c_str(), MAX_PATH, buffer, &pTemp);
            fullPath = std::string(buffer);
        }

        if(fullPath.at(fullPath.length()-1) != '\\')
            fullPath += "\\";

    #else

        /* Non-Windows. If begins with / it is an absolute path, otherwise a relative path */
        if(path.at(0) == '/')
            fullPath = path;
        else
        {
            std::string currentDir(getcwd(buffer, MAX_PATH));
            fullPath = currentDir + "/" + path;
        }

        if(fullPath.at(fullPath.length()-1) != '/')
            fullPath += "/";

    #endif

        return fullPath;
    }


    /* Returns the full pathname of the PID file */
    std::string GetPidFile()
    {
        LOCK(FILESYSTEM_MUTEX);

        std::string pathPidFile(config::GetArg("-pid", "Nexus.pid"));
        return config::GetDataDir() + "/" +pathPidFile;
    }


    /* Creates a PID file on disk for the provided PID */
    void CreatePidFile(const std::string &path, pid_t pid)
    {
        LOCK(FILESYSTEM_MUTEX);

        FILE* file = fopen(path.c_str(), "w");
        if(file)
        {
        #ifndef WIN32
            fprintf(file, "%d", pid);
        #else
            /* For some reason, PRI64d fails with warning here because %I non-ANSI compliant,
               but it doesn't give this warning in other places except for config.cpp
               perhaps because this is fprintf (debug::log for example does not use printf).

               Consider re-writing this to use << operator

               If we just change to llu, then Linux gives warnings, so instead use a
               conditional compile and get warnings out of both */
            fprintf(file, "%llu", pid);
        #endif

            fclose(file);
        }
    }

}
