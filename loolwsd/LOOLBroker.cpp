#include <sys/types.h>
#include <sys/wait.h>
#include <sys/capability.h>

#include <utime.h>
#include <ftw.h>
#include <unistd.h>

#include <mutex>
#include <cstring>
#include <cassert>
#include <iostream>
#include <fstream>

#include <Poco/Types.h>
#include <Poco/Random.h>
#include <Poco/Path.h>
#include <Poco/File.h>
#include <Poco/ThreadLocal.h>
#include <Poco/Process.h>
#include <Poco/Thread.h>
#include <Poco/SharedMemory.h>

#include "Util.hpp"

using Poco::Path;
using Poco::File;
using Poco::ThreadLocal;
using Poco::Process;
using Poco::Thread;
using Poco::ProcessHandle;

namespace
{
    ThreadLocal<std::string> sourceForLinkOrCopy;
    ThreadLocal<Path> destinationForLinkOrCopy;

    int linkOrCopyFunction(const char *fpath,
                           const struct stat *sb,
                           int typeflag,
                           struct FTW *ftwbuf)
    {
        if (strcmp(fpath, sourceForLinkOrCopy->c_str()) == 0)
            return 0;

        assert(fpath[strlen(sourceForLinkOrCopy->c_str())] == '/');
        const char *relativeOldPath = fpath + strlen(sourceForLinkOrCopy->c_str()) + 1;

#ifdef __APPLE__
        if (strcmp(relativeOldPath, "PkgInfo") == 0)
            return 0;
#endif

        Path newPath(*destinationForLinkOrCopy, Path(relativeOldPath));

        switch (typeflag)
        {
        case FTW_F:
            File(newPath.parent()).createDirectories();
            if (link(fpath, newPath.toString().c_str()) == -1)
            {
                std::cout << Util::logPrefix() +
                                                       "link(\"" + fpath + "\",\"" + newPath.toString() + "\") failed: " +
                                                       strerror(errno) << std::endl;
                exit(1);
            }
            break;
        case FTW_DP:
            {
                struct stat st;
                if (stat(fpath, &st) == -1)
                {
                    std::cout << Util::logPrefix() +
                                                           "stat(\"" + fpath + "\") failed: " +
                                                           strerror(errno) << std::endl;
                    return 1;
                }
                File(newPath).createDirectories();
                struct utimbuf ut;
                ut.actime = st.st_atime;
                ut.modtime = st.st_mtime;
                if (utime(newPath.toString().c_str(), &ut) == -1)
                {
                    std::cout << Util::logPrefix() +
                                                           "utime(\"" + newPath.toString() + "\", &ut) failed: " +
                                                           strerror(errno) << std::endl;
                    return 1;
                }
            }
            break;
        case FTW_DNR:
            std::cout <<Util::logPrefix() +
                                                   "Cannot read directory '" + fpath + "'" << std::endl;
            return 1;
        case FTW_NS:
            std::cout <<Util::logPrefix() +
                                                   "nftw: stat failed for '" + fpath + "'" << std::endl;
            return 1;
        case FTW_SLN:
            std::cout <<Util::logPrefix() +
                                                         "nftw: symlink to nonexistent file: '" + fpath + "', ignored" << std::endl;
            break;
        default:
            assert(false);
        }
        return 0;
    }

    void linkOrCopy(const std::string& source, const Path& destination)
    {
        *sourceForLinkOrCopy = source;
        if (sourceForLinkOrCopy->back() == '/')
            sourceForLinkOrCopy->pop_back();
        *destinationForLinkOrCopy = destination;
        if (nftw(source.c_str(), linkOrCopyFunction, 10, FTW_DEPTH) == -1)
            std::cout << Util::logPrefix() +
                                                   "linkOrCopy: nftw() failed for '" + source + "'" << std::endl;
    }

    void dropCapability(
#ifdef __linux
                        cap_value_t capability
#endif
                        )
    {
#ifdef __linux
        cap_t caps;
        cap_value_t cap_list[] = { capability };

        caps = cap_get_proc();
        if (caps == NULL)
        {
            std::cout << Util::logPrefix() + "cap_get_proc() failed: " + strerror(errno) << std::endl;
            exit(1);
        }

        if (cap_set_flag(caps, CAP_EFFECTIVE, sizeof(cap_list)/sizeof(cap_list[0]), cap_list, CAP_CLEAR) == -1 ||
            cap_set_flag(caps, CAP_PERMITTED, sizeof(cap_list)/sizeof(cap_list[0]), cap_list, CAP_CLEAR) == -1)
        {
            std::cout << Util::logPrefix() +  "cap_set_flag() failed: " + strerror(errno) << std::endl;
            exit(1);
        }

        if (cap_set_proc(caps) == -1)
        {
            std::cout << std::string("cap_set_proc() failed: ") + strerror(errno) << std::endl;
            exit(1);
        }

        char *capText = cap_to_text(caps, NULL);
        std::cout <<Util::logPrefix() + "Capabilities now: " + capText << std::endl;
        cap_free(capText);

        cap_free(caps);
#endif
        // We assume that on non-Linux we don't need to be root to be able to hardlink to files we
        // don't own, so drop root.
        if (geteuid() == 0 && getuid() != 0)
        {
            // The program is setuid root. Not normal on Linux where we use setcap, but if this
            // needs to run on non-Linux Unixes, setuid root is what it will bneed to be to be able
            // to do chroot().
            if (setuid(getuid()) != 0) {
                std::cout << std::string("setuid() failed: ") + strerror(errno) << std::endl;
            }
        }
#if ENABLE_DEBUG
        if (geteuid() == 0 && getuid() == 0)
        {
#ifdef __linux
            // Argh, awful hack
            if (capability == CAP_FOWNER)
                return;
#endif

            // Running under sudo, probably because being debugged? Let's drop super-user rights.
            LOOLWSD::runningAsRoot = true;
            if (LOOLWSD::uid == 0)
            {
                struct passwd *nobody = getpwnam("nobody");
                if (nobody)
                    LOOLWSD::uid = nobody->pw_uid;
                else
                    LOOLWSD::uid = 65534;
            }
            if (setuid(LOOLWSD::uid) != 0) {
                std::cout << std::string("setuid() failed: ") + strerror(errno) << std::endl;
            }
        }
#endif
    }
}

static std::map<Poco::Process::PID, Poco::UInt64> _childProcesses;


static int prefixcmp(const char *str, const char *prefix)
{
	for (; ; str++, prefix++)
		if (!*prefix)
			return 0;
		else if (*str != *prefix)
			return (unsigned char)*prefix - (unsigned char)*str;
}


static int createLibreOfficeKit()
{
    Process::Args args;
    //args.push_back("--losubpath=" + LOOLWSD::loSubPath);
    //args.push_back("--systemplate=" + sysTemplate);
    //args.push_back("--lotemplate=" + loTemplate);
    //args.push_back("--childroot=" + childRoot);
    //args.push_back("--numprespawns=" + std::to_string(_numPreSpawnedChildren));

    std::string executable = "loolkit";
    
    /*if (!File(executable).exists())
    {
      std::cout << Util::logPrefix() + "Error loolkit does not exists" << std::endl;      
      return -1;
    }*/
    
	  //Process::Env env;
    //env["LD_LIBRARY_PATH"] = "/usr/local/lib";
    //env["LD_DEBUG"] = "libs";
    
    std::cout << Util::logPrefix() + "Launching LibreOfficeKit: " + executable + " " + Poco::cat(std::string(" "), args.begin(), args.end()) << std::endl;
    
    //ProcessHandle child = Process::launch(executable, args, "/usr/bin/", NULL, NULL, NULL, env);
    ProcessHandle child = Process::launch(executable, args);
    
    _childProcesses[child.id()] = child.id();
    return 0;
}

static void startupLibreOfficeKit(int nLOKits)
{
    for (int nCntr = nLOKits; nCntr; nCntr--)
    {
        if (createLibreOfficeKit() < 0)
            break;
    }
}

// Broker process
int main(int argc, char** argv)
{
    // Initialization
    std::mutex _rngMutex;
    Poco::Random _rng;
    std::string childRoot;
    std::string loSubPath;
    std::string sysTemplate;
    std::string loTemplate;
    int _numPreSpawnedChildren = 0;
    Poco::SharedMemory _sharedForkChild("loolwsd", sizeof(bool), Poco::SharedMemory::AM_WRITE);
    
    while (argc > 0) 
    {
		  char *cmd = argv[0];
		  char *eq  = NULL;
		  if (strstr(cmd, "loolbroker"))
      {
        
      }		  
      if (!prefixcmp(cmd, "--losubpath=")) 
      {
        eq = strchrnul(cmd, '=');
        if (*eq)
          loSubPath = std::string(++eq);
      }
      else if (!prefixcmp(cmd, "--systemplate="))
      {
        eq = strchrnul(cmd, '=');
        if (*eq)
          sysTemplate = std::string(++eq);
      }
      else if (!prefixcmp(cmd, "--lotemplate="))
      {
        eq = strchrnul(cmd, '=');
        if (*eq)
          loTemplate = std::string(++eq);
      }
      else if (!prefixcmp(cmd, "--childroot="))
      {
        eq = strchrnul(cmd, '=');
        if (*eq)
          childRoot = std::string(++eq);
      }
      else if (!prefixcmp(cmd, "--numprespawns="))
      {
        eq = strchrnul(cmd, '=');
        if (*eq)
          _numPreSpawnedChildren = std::stoi(std::string(++eq));
      }
      
		  argv++;
		  argc--;
    }
   
   if (loSubPath.empty())
   {
     std::cout << "--losubpath is empty" << std::endl;
     exit(1);
   }
    
   if (sysTemplate.empty())
   {
     std::cout << "--systemplate is empty" << std::endl;
     exit(1);
   }

   if (loTemplate.empty())
   {
     std::cout << "--lotemplate is empty" << std::endl;
     exit(1);
   }

   if (childRoot.empty())
   {
     std::cout << "--childroot is empty" << std::endl;
     exit(1);
   }

   if ( !_numPreSpawnedChildren )
   {
     std::cout << "--numprespawns is 0" << std::endl;
     exit(1);
   }

    std::unique_lock<std::mutex> rngLock(_rngMutex);
    Poco::UInt64 _childId = (((Poco::UInt64)_rng.next()) << 32) | _rng.next() | 1;
    rngLock.unlock();
    

    Path jail = Path::forDirectory(childRoot + Path::separator() + std::to_string(_childId));
    File(jail).createDirectories();

    Path jailLOInstallation(jail, loSubPath);
    jailLOInstallation.makeDirectory();
    File(jailLOInstallation).createDirectory();

    // Copy (link) LO installation and other necessary files into it from the template

    linkOrCopy(sysTemplate, jail);
    linkOrCopy(loTemplate, jailLOInstallation);

#ifdef __linux
    // Create the urandom and random devices
    File(Path(jail, "/dev")).createDirectory();
    if (mknod((jail.toString() + "/dev/random").c_str(),
                S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
                makedev(1, 8)) != 0)
    {
        std::cout << Util::logPrefix() +
                "mknod(" + jail.toString() + "/dev/random) failed: " +
                strerror(errno) << std::endl;

    }
    if (mknod((jail.toString() + "/dev/urandom").c_str(),
                S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
                makedev(1, 9)) != 0)
    {
        std::cout << Util::logPrefix() +
                "mknod(" + jail.toString() + "/dev/urandom) failed: " +
                strerror(errno) << std::endl;
    }
#endif

    std::cout << "desktopMain -> chroot(\"" + jail.toString() + "\")" << std::endl;
    if (chroot(jail.toString().c_str()) == -1)
    {
        std::cout << "chroot(\"" + jail.toString() + "\") failed: " + strerror(errno) << std::endl;
        exit(-1);
    }

    if (chdir("/") == -1)
    {
        std::cout << std::string("chdir(\"/\") in jail failed: ") + strerror(errno) << std::endl;
        exit(-1);
    }

#ifdef __linux
    dropCapability(CAP_SYS_CHROOT);
#else
    dropCapability();
#endif

    if (std::getenv("SLEEPFORDEBUGGER"))
    {
        std::cout << "Sleeping " << std::getenv("SLEEPFORDEBUGGER") << " seconds, " <<
            "attach process " << Process::id() << " in debugger now." << std::endl;
        Thread::sleep(std::stoul(std::getenv("SLEEPFORDEBUGGER")) * 1000);
    }

    startupLibreOfficeKit(_numPreSpawnedChildren);

    while (_childProcesses.size() > 0)
    {
        int status;
        pid_t pid = waitpid(-1, &status, WUNTRACED | WNOHANG);
        if (pid > 0)
        {
            if ( _childProcesses.find(pid) != _childProcesses.end() )
            {
                if ((WIFEXITED(status) || WIFSIGNALED(status) || WTERMSIG(status) ) )
                {
                    std::cout << Util::logPrefix() << "One of our known child processes died :" << std::to_string(pid)  << std::endl;
                    _childProcesses.erase(pid);
                }

                if ( WCOREDUMP(status) )
                    std::cout << Util::logPrefix() << "The child produced a core dump." << std::endl;

                if ( WIFSTOPPED(status) )
                    std::cout << Util::logPrefix() << "The child process was stopped by delivery of a signal." << std::endl;

                if ( WSTOPSIG(status) )
                    std::cout << Util::logPrefix() << "The child process was stopped." << std::endl;

                if ( WIFCONTINUED(status) )
                    std::cout << Util::logPrefix() << "The child process was resumed." << std::endl;
            }
            else
            {
                std::cout << Util::logPrefix() << "None of our known child processes died :" << std::to_string(pid) << std::endl;
            }
        }
        else if (pid < 0)
            std::cout << Util::logPrefix() << "Child error: " << strerror(errno) << std::endl;

        if ( _sharedForkChild.begin()[0] )
        {
            _sharedForkChild.begin()[0] = 0;
            std::cout << Util::logPrefix() << "No availabe child session, fork new one" << std::endl;
            if (createLibreOfficeKit() < 0 )
                break;
        }
    }
    
    // Terminate child processes
    for (auto i : _childProcesses)
    {
        std::cout << Util::logPrefix() + "Requesting child process " + std::to_string(i.first) + " to terminate" << std::endl;
        Process::requestTermination(i.first);
    }
    
    std::cout << Util::logPrefix() << "loolbroker finished OK!" << std::endl;
    return 0;
}