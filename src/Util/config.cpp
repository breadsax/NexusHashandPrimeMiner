/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <Util/include/args.h>
#include <Util/include/config.h>
#include <Util/include/mutex.h>
#include <Util/include/filesystem.h>
#include <Util/include/debug.h>
#include <fstream>
#include <cstring> /* strlen */

#ifdef WIN32
#include <shlobj.h>
#endif

namespace config
{

    /* Read the Config file from the Disk. */
    void ReadConfigFile(std::map<std::string, std::string>& mapSettingsRet,
                        std::map<std::string, std::vector<std::string> >& mapMultiSettingsRet)
    {
        std::ifstream streamConfig(GetConfigFile());
        if(!streamConfig.is_open())
            return; /* No nexus.conf file is OK */

        std::string line;

        while(!streamConfig.eof())
        {
            std::getline(streamConfig, line);

            if(streamConfig.eof())
                break;

            size_t i = line.find('=');
            if(i == std::string::npos)
                continue;

            std::string strKey = std::string("-") + std::string(line, 0, i);
            std::string strVal = std::string(line, i + 1, line.size() - i - 1);

            if(mapSettingsRet.count(strKey) == 0)
            {
                mapSettingsRet[strKey] = strVal;
                /* interpret nofoo=1 as foo=0 (and nofoo=0 as foo=1) as long as foo not set */
                InterpretNegativeSetting(strKey, mapSettingsRet);
            }

            mapMultiSettingsRet[strKey].push_back(strVal);
        }
        streamConfig.close();
    }


    /* Read the Config file from the Disk, loading -datadir setting if provided. */
    void ReadConfigFile(std::map<std::string, std::string>& mapSettingsRet,
                        std::map<std::string, std::vector<std::string> >& mapMultiSettingsRet,
                        const int argc, const char*const argv[])
    {
        /* This may be called before parsing command line arguments.
         * Check if -datadir option is present and record the setting if it is
         */
        for (int i = 1; i < argc; ++i)
        {
            const std::string argValue(argv[i]);
            const std::string datadirSwitch("-datadir");
            const std::string configFileSwitch("-conf");

            /* Form is -datadir=path so path setting starts at location 9.
             * Any length <9 on argv is either a different option or datadir with no value. Ignore these.
             */
            if (argValue.length() > 9 && argValue.compare(0, 8, datadirSwitch) == 0)
                mapArgs[datadirSwitch] = argValue.substr(9, std::string::npos);

            /* Also need to do the same for custom config file name */
            if (argValue.length() > 6 && argValue.compare(0, 5, configFileSwitch) == 0)
                mapArgs[configFileSwitch] = argValue.substr(6, std::string::npos);
        }

        ReadConfigFile(mapSettingsRet, mapMultiSettingsRet);
    }


    /* Setup PID file for Linux users. */
    void CreatePidFile(const std::string &path, pid_t pid)
    {
        FILE* file = fopen(path.c_str(), "w");
        if (file)
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


    #ifdef WIN32

    /* Get the system folder path to the special Appdata directory */
    std::string MyGetSpecialFolderPath(int nFolder, bool fCreate)
    {
        char pszPath[MAX_PATH] = "";
        std::string p;

        if (SHGetSpecialFolderPathA(nullptr, pszPath, nFolder, fCreate))
            p = pszPath;

        else if (nFolder == CSIDL_STARTUP)
        {
            p = getenv("USERPROFILE");
            p.append("\\Start Menu");
            p.append("\\Programs");
            p.append("\\Startup");
        }
        else if (nFolder == CSIDL_APPDATA)
            p = getenv("APPDATA");

        return p;
    }
    #endif


    /* Get the default directory Nexus data is stored in. */
    std::string GetDefaultDataDir(std::string strName)
    {
        /* Windows: C:\Documents and Settings\username\Application Data\Nexus
         * Mac: ~/Library/Application Support/Nexus
         * Unix: ~/.Nexus */
    std::string pathRet;
    #ifdef WIN32
        // Windows
        pathRet = MyGetSpecialFolderPath(CSIDL_APPDATA, true);
        pathRet.append("\\" + strName + "\\");
    #else
        char* pszHome = getenv("HOME");
        if (pszHome == nullptr || strlen(pszHome) == 0)
            pathRet = "/";
        else
            pathRet = pszHome;
    #ifdef MAC_OSX
        // Mac
        pathRet.append("/Library/Application Support");
        filesystem::create_directories(pathRet);
        pathRet.append("/" + strName + "/");
    #else
        // Unix
        pathRet.append("/." + strName + "/");
    #endif
    #endif

        return pathRet;
    }


    /* Get the Location of the Config File. */
    std::string GetConfigFile()
    {
        static bool fPathLogged = false; //Used so we only log the conf file once

        std::string pathConfigFile(GetDataDir(false));

        pathConfigFile.append(GetArg("-conf", "nexus.conf"));

        if (!fPathLogged && filesystem::exists(pathConfigFile))
        {
            debug::log(0, "Using configuration file: ", pathConfigFile);
            fPathLogged = true;
        }

        return pathConfigFile;
    }


    /* Get the Location of the PID File. */
    std::string GetPidFile()
    {
        std::string pathPidFile(GetDataDir());

        pathPidFile.append(GetArg("-pid", "nexus.pid"));

        return pathPidFile;
    }


    /* Get the location that Nexus data is being stored in. */
    std::string GetDataDir(bool fNetSpecific)
    {
        static std::string pathCached[2];
        static std::mutex csPathCached;
        static bool fCachedPath[2] = {false, false};

        std::string &path = pathCached[fNetSpecific];

        // This can be called during exceptions by debug log, so we cache the
        // value so we don't have to do memory allocations after that.
        if (fCachedPath[fNetSpecific])
            return path;

        LOCK(csPathCached);
        if (mapArgs.count("-datadir"))
        {
            /* get the command line argument for the data directory */
            path = mapArgs["-datadir"];

            if (path.length() == 0)
            {
                /* -datadir setting used, but no value provided */
                debug::error(FUNCTION, "-datadir no path specified. Using default.");
                path = GetDefaultDataDir();
            }
            else
            {
                /* build the system complete path to the data directory */
                path = filesystem::system_complete(path);
            }

            /* Validate the resulting path length */
            if  (path.length() > MAX_PATH)
            {
                debug::error(FUNCTION, "-datadir path exceeds maximum allowed path length. Using default.");
                path = GetDefaultDataDir();
            }
        }
        else
            path = GetDefaultDataDir();

        if (fNetSpecific && GetBoolArg("-testnet", false))
            path.append("testnet/");

        filesystem::create_directories(path);

        pathCached[fNetSpecific] =  path;
        fCachedPath[fNetSpecific] = true;
        return path;
    }


    /*  Get the comma seperated values from a string. */
    void CommaSeperatedValues(std::vector<uint32_t> &values, std::string &strCSV)
    {
        std::stringstream ss(strCSV);
        uint32_t i = 0;

        if(ss.peek() == ',')
            ss.ignore();

        while(ss >> i)
        {
            values.push_back(i);

            if(ss.peek() == ',')
                ss.ignore();

        }
    }
}
