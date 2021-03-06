#ifdef WIN_SIM
    #include <direct.h>
    #include <Windows.h>
    #include <process.h>
    #define THREAD_RET_TYPE void
    #define THREAD_RET_VAL void()
#else
    #include <unistd.h>
    #include <pthread.h>
    #define THREAD_RET_TYPE void*
    #define THREAD_RET_VAL 0
#endif

#ifdef SIM_WITHOUT_QT_AT_ALL
    #include <algorithm>
    #ifdef WIN_SIM
        #include "_dirent.h"
    #else
        #include <dirent.h>
    #endif
#else
    #include <QtCore/QCoreApplication>
    #include <QLibrary>
    #include <QFileInfo>
    #include <QDir>
#endif

// Following required to have Lua extension libraries work under Linux. Strange...
#ifdef LIN_SIM
    extern "C" {
        #include "lua.h"
        #include "lualib.h"
        #include "lauxlib.h"
    }
    void dummyFunction()
    {
        lua_State *L;
        L=luaL_newstate();
    }
#endif

#include "simLib.h"
#include <vector>
#include <iostream>

static LIBRARY simLib=nullptr;
static int stopDelay;
static int options;
static std::string sceneOrModelToLoad;

static simVoid(*simulatorInit)()=nullptr; // use default initialization callback routine
/* You may provide your own initialization callback routine, e.g.:
std::vector<int> pluginHandles;
int loadPlugin(const char* theName,const char* theDirAndName)
{
    std::string nm(theName);
    nm="simExt"+nm;
    std::string tmp;
    tmp=nm+": loading...";
    simAddLog("CoppeliaSimClient",sim_verbosity_loadinfos,tmp.c_str());
    int pluginHandle=simLoadModule(theDirAndName,theName);
    if (pluginHandle==-3)
    #ifdef WIN_SIM
        tmp=nm+": load failed (could not load). The plugin probably couldn't load dependency libraries. Try rebuilding the plugin.";
        simAddLog("CoppeliaSimClient",sim_verbosity_errors,tmp.c_str());
    #endif
    #ifdef MAC_SIM
        tmp=nm+": load failed (could not load). The plugin probably couldn't load dependency libraries. Try 'otool -L pluginName.dylib' for more infos, or simply rebuild the plugin.";
        simAddLog("CoppeliaSimClient",sim_verbosity_errors,tmp.c_str());
    #endif
    #ifdef LIN_SIM
        tmp=nm+": load failed (could not load). The plugin probably couldn't load dependency libraries. For additional infos, modify the script 'libLoadErrorCheck.sh', run it and inspect the output.";
        simAddLog("CoppeliaSimClient",sim_verbosity_errors,tmp.c_str());
    #endif

    if (pluginHandle==-2)
        tmp=nm+": load failed (missing entry points).";
        simAddLog("CoppeliaSimClient",sim_verbosity_errors,tmp.c_str());
    if (pluginHandle==-1)
        tmp=nm+": load failed (failed initialization).";
        simAddLog("CoppeliaSimClient",sim_verbosity_errors,tmp.c_str());
    if (pluginHandle>=0)
        tmp=nm+": load succeeded.";
        simAddLog("CoppeliaSimClient",sim_verbosity_loadinfos,tmp.c_str());
    return(pluginHandle);
}

void simulatorInit()
{
    simAddLog("CoppeliaSimClient",sim_verbosity_loadinfos,"simulator launched.");
    std::vector<std::string> theNames;
    std::vector<std::string> theDirAndNames;
#ifdef SIM_WITHOUT_QT_AT_ALL
    char curDirAndFile[2048];
    #ifdef WIN_SIM
        GetModuleFileNameA(NULL,curDirAndFile,2000);
        int i=0;
        while (true)
        {
            if (curDirAndFile[i]==0)
                break;
            if (curDirAndFile[i]=='\\')
                curDirAndFile[i]='/';
            i++;
        }
        std::string theDir(curDirAndFile);
        while ( (theDir.size()>0)&&(theDir[theDir.size()-1]!='/') )
            theDir.erase(theDir.end()-1);
        if (theDir.size()>0)
            theDir.erase(theDir.end()-1);
    #else
        getcwd(curDirAndFile,2000);
        std::string theDir(curDirAndFile);
    #endif

    DIR* dir;
    struct dirent* ent;
    if ((dir=opendir(theDir.c_str()))!=NULL)
    {
        while ((ent=readdir(dir))!=NULL)
        {
            if ( (ent->d_type==DT_LNK)||(ent->d_type==DT_REG) )
            {
                std::string nm(ent->d_name);
                std::transform(nm.begin(),nm.end(),nm.begin(),::tolower);
                int pre=0;
                int po=0;
                #ifdef WIN_SIM
                if ( boost::algorithm::starts_with(nm,"simext")&&boost::algorithm::ends_with(nm,".dll") )
                    pre=6;po=4;
                #endif
                #ifdef LIN_SIM
                if ( boost::algorithm::starts_with(nm,"libsimext")&&boost::algorithm::ends_with(nm,".so") )
                    pre=9;po=3;
                #endif
                #ifdef MAC_SIM
                if ( boost::algorithm::starts_with(nm,"libsimext")&&boost::algorithm::ends_with(nm,".dylib") )
                    pre=9;po=6;
                #endif
                if (pre!=0)
                {
                    nm=ent->d_name;
                    nm.assign(nm.begin()+pre,nm.end()-po);
                    if (nm.find('_')==std::string::npos)
                    {
                        theNames.push_back(nm);
                        theDirAndNames.push_back(theDir+'/'+ent->d_name);
                    }
                }
            }
        }
        closedir(dir);
    }
#else // SIM_WITHOUT_QT_AT_ALL

    QFileInfo pathInfo(QCoreApplication::applicationFilePath());
    std::string pa=pathInfo.path().toStdString();
    QDir dir(pa.c_str());
    dir.setFilter(QDir::Files|QDir::Hidden);
    dir.setSorting(QDir::Name);
    QStringList filters;
    int bnl=6;
    #ifdef WIN_SIM
        std::string tmp("simExt*.dll");
    #endif
    #ifdef MAC_SIM
        std::string tmp("libsimExt*.dylib");
        bnl=9;
    #endif
    #ifdef LIN_SIM
        std::string tmp("libsimExt*.so");
        bnl=9;
    #endif
    filters << tmp.c_str();
    dir.setNameFilters(filters);
    QFileInfoList list=dir.entryInfoList();
    for (int i=0;i<list.size();++i)
    {
        QFileInfo fileInfo=list.at(i);
        std::string bla(fileInfo.baseName().toLocal8Bit());
        std::string tmp;
        tmp.assign(bla.begin()+bnl,bla.end());
        if (tmp.find('_')==std::string::npos)
        {
            theNames.push_back(tmp);
            theDirAndNames.push_back(fileInfo.absoluteFilePath().toLocal8Bit().data());
        }
    }
#endif // SIM_WITHOUT_QT_AT_ALL

    // Load the system plugins first:
    for (size_t i=0;i<theNames.size();i++)
    {
        if ( (theNames[i].compare("MeshCalc")==0)||(theNames[i].compare("Dynamics")==0) )
        {
            int pluginHandle=loadPlugin(theNames[i].c_str(),theDirAndNames[i].c_str());
            if (pluginHandle>=0)
                pluginHandles.push_back(pluginHandle);
            theDirAndNames[i]=""; // mark as 'already loaded'
        }
    }
    simLoadModule("",""); // indicate that we are done with the system plugins

    // Now load the other plugins too:
    for (size_t i=0;i<theNames.size();i++)
    {
        if (theDirAndNames[i].compare("")!=0)
        { // not yet loaded
            int pluginHandle=loadPlugin(theNames[i].c_str(),theDirAndNames[i].c_str());
            if (pluginHandle>=0)
                pluginHandles.push_back(pluginHandle);
        }
    }

    if (sceneOrModelToLoad.length()!=0)
    { // Here we double-clicked a CoppeliaSim file or dragged-and-dropped it onto this application
        if ( boost::algorithm::ends_with(sceneOrModelToLoad.c_str(),".ttt")||boost::algorithm::ends_with(sceneOrModelToLoad.c_str(),".simscene.xml") )
        {
            if (simLoadScene(sceneOrModelToLoad.c_str())==-1)
                simAddLog("CoppeliaSimClient",sim_verbosity_errors,"scene could not be opened.");
        }
        if ( boost::algorithm::ends_with(sceneOrModelToLoad.c_str(),".ttm")||boost::algorithm::ends_with(sceneOrModelToLoad.c_str(),".simmodel.xml"))
        {
            if (simLoadModel(sceneOrModelToLoad.c_str())==-1)
                simAddLog("CoppeliaSimClient",sim_verbosity_errors,"model could not be opened.");
        }
    }
}
*/

static simVoid(*simulatorDeinit)()=nullptr; // use default deinitialization callback routine
/* You may provide your own deinitialization callback routine, e.g.:
void simulatorDeinit()
{
    // Unload all plugins:
    while (pluginHandles.size()>0)
    {
        simUnloadModule(pluginHandles[pluginHandles.size()-1]);
        pluginHandles.pop_back();
    }
    simAddLog("CoppeliaSimClient",sim_verbosity_loadinfos,"simulator ended.");
}
*/

static simVoid(*simulatorLoop)()=nullptr; // use default simulation loop callback routine
/* You may provide your own simulation loop callback routine, e.g.:
void simulatorLoop()
{   // The main application loop (excluding the GUI part)
    static bool wasRunning=false;
    int auxValues[4];
    int messageID=0;
    int dataSize;
    if (options&sim_autostart)
    {
        simStartSimulation();
        options-=sim_autostart;
    }
    while (messageID!=-1)
    {
        simChar* data=simGetSimulatorMessage(&messageID,auxValues,&dataSize);
        if (messageID!=-1)
        {
            if (messageID==sim_message_simulation_start_resume_request)
                simStartSimulation();
            if (messageID==sim_message_simulation_pause_request)
                simPauseSimulation();
            if (messageID==sim_message_simulation_stop_request)
                simStopSimulation();
            if (data!=NULL)
                simReleaseBuffer(data);
        }
    }

    // Handle a running simulation:
    if ( (simGetSimulationState()&sim_simulation_advancing)!=0 )
    {
        wasRunning=true;
        if ( (simGetRealTimeSimulation()!=1)||(simIsRealTimeSimulationStepNeeded()==1) )
        {
            if ((simHandleMainScript()&sim_script_main_script_not_called)==0)
                simAdvanceSimulationByOneStep();
            if ((stopDelay>0)&&(simGetSimulationTime()>=float(stopDelay)/1000.0f))
            {
                stopDelay=0;
                simStopSimulation();
            }
        }
    }
    if ( (simGetSimulationState()==sim_simulation_stopped)&&wasRunning&&((options&sim_autoquit)!=0) )
    {
        wasRunning=false;
        simQuitSimulator(true); // will post the quit command
    }
}
*/

int loadSimLib(const char* execPath,std::string& appDir)
{
    printf("[CoppeliaSimClient]    loading the CoppeliaSim library...\n"); // always print, do not use simAddLog
//    simAddLog("CoppeliaSimClient",sim_verbosity_loadinfos,"loading the CoppeliaSim library...");
    #ifdef WIN_SIM
        // Set the current path same as the application path
        char basePath[2048];
        _fullpath(basePath,execPath,sizeof(basePath));
        std::string p(basePath);
        p.erase(p.begin()+p.find_last_of('\\')+1,p.end());
        QDir::setCurrent(p.c_str());
    #endif
    appDir=execPath;
    while ((appDir.length()>0)&&(appDir[appDir.length()-1]))
    {
        int l=(int)appDir.length();
        if (appDir[l-1]!='/')
            appDir.erase(appDir.end()-1);
        else
        { // we might have a 2 byte char:
            if (l>1)
            {
                if (appDir[l-2]>0x7F)
                    appDir.erase(appDir.end()-1);
                else
                    break;

            }
            else
                break;
        }
    }
    chdir(appDir.c_str());
    #ifdef WIN_SIM
        simLib=loadSimLibrary("coppeliaSim");
    #endif
    #ifdef MAC_SIM
        simLib=loadSimLibrary("libcoppeliaSim.dylib");
    #endif
    #ifdef LIN_SIM
        simLib=loadSimLibrary("libcoppeliaSim.so");
    #endif
    if (simLib!=NULL)
    {
        if (getSimProcAddresses(simLib)!=0)
        {
            printf("[CoppeliaSimClient]    done.\n"); // always print, do not use simAddLog
//            simAddLog("CoppeliaSimClient",sim_verbosity_loadinfos,"done.");
            return(1);
        }
        printf("[CoppeliaSimClient]    could not find all required functions in the CoppeliaSim library.\n"); // always print, do not use simAddLog
//        simAddLog("CoppeliaSimClient",sim_verbosity_errors,"could not find all required functions in the CoppeliaSim library.");
        return(0);
    }
    printf("[CoppeliaSimClient]    could not find or correctly load the CoppeliaSim library.\n"); // always print, do not use simAddLog
//    simAddLog("CoppeliaSimClient",sim_verbosity_errors,"could not find or correctly load the CoppeliaSim library.");
    return(-1);
}

void unloadSimLib()
{
    printf("[CoppeliaSimClient]    unloading the CoppeliaSim library...\n"); // always print, do not use simAddLog
//    simAddLog("CoppeliaSimClient",sim_verbosity_loadinfos,"unloading the CoppeliaSim library...");
    unloadSimLibrary(simLib);
    printf("[CoppeliaSimClient]    done.\n"); // always print, do not use simAddLog
//    simAddLog("CoppeliaSimClient",sim_verbosity_loadinfos,"done.");
}

bool run(int argc,char* argv[],const char* appDir,bool uiOnly)
{
    options=sim_gui_all;
    stopDelay=0;
    for (int i=1;i<argc;i++)
    {
        std::string arg(argv[i]);
        if (arg[0]=='-')
        {
            if (arg.length()>=2)
            {
                if (arg[1]=='h')
                {
                    options|=sim_gui_all|sim_gui_headless;
                    options-=sim_gui_all;
                }
                if (arg[1]=='s')
                {
                    options|=sim_autostart;
                    stopDelay=0;
                    unsigned int p=2;
                    while (arg.length()>p)
                    {
                        stopDelay*=10;
                        if ((arg[p]>='0')&&(arg[p]<='9'))
                            stopDelay+=arg[p]-'0';
                        else
                        {
                            stopDelay=0;
                            break;
                        }
                        p++;
                    }
                }
                if (arg[1]=='q')
                    options|=sim_autoquit;
                if (arg[1]=='c')
                {
                    std::string tmp;
                    tmp.assign(arg.begin()+2,arg.end());
                    simSetStringParameter(sim_stringparam_startupscriptstring,tmp.c_str());
                }
                if (arg[1]=='v')
                {
                    std::string tmp;
                    tmp.assign(arg.begin()+2,arg.end());
                    simSetStringParameter(sim_stringparam_verbosity,tmp.c_str());
                }
                if (arg[1]=='w')
                {
                    std::string tmp;
                    tmp.assign(arg.begin()+2,arg.end());
                    simSetStringParameter(sim_stringparam_statusbarverbosity,tmp.c_str());
                }
                if (arg[1]=='x')
                {
                    std::string tmp;
                    tmp.assign(arg.begin()+2,arg.end());
                    simSetStringParameter(sim_stringparam_dlgverbosity,tmp.c_str());
                }
                if ((arg[1]=='a')&&(arg.length()>2))
                {
                    std::string tmp;
                    tmp.assign(arg.begin()+2,arg.end());
                    simSetStringParameter(sim_stringparam_additional_addonscript1,tmp.c_str()); // normally, never call API functions before simRunSimulator!!
                }
                if ((arg[1]=='b')&&(arg.length()>2))
                {
                    std::string tmp;
                    tmp.assign(arg.begin()+2,arg.end());
                    simSetStringParameter(sim_stringparam_additional_addonscript2,tmp.c_str()); // normally, never call API functions before simRunSimulator!!
                }
                if ((arg[1]=='g')&&(arg.length()>2))
                {
                    static int cnt=0;
                    std::string tmp;
                    tmp.assign(arg.begin()+2,arg.end());
                    if (cnt<9)
                        simSetStringParameter(sim_stringparam_app_arg1+cnt,tmp.c_str()); // normally, never call API functions before simRunSimulator!!
                    cnt++;
                }
                if ((arg[1]=='G')&&(arg.length()>3))
                {
                    size_t pos=arg.find('=',3);
                    if ( (pos!=std::string::npos)&&(pos!=arg.length()-1) )
                    {
                        std::string key(arg.begin()+2,arg.begin()+pos);
                        std::string param(arg.begin()+pos+1,arg.end());
                        simSetStringNamedParam(key.c_str(),param.c_str(),int(param.size()));
                    }
                }
            }
        }
        else
        {
            if (sceneOrModelToLoad.length()==0)
                sceneOrModelToLoad=arg;
        }
    }

    simAddLog("CoppeliaSimClient",sim_verbosity_loadinfos,"launching CoppeliaSim...");
    if (!uiOnly)
    {
        if (simRunSimulatorEx("CoppeliaSim",options,simulatorInit,simulatorLoop,simulatorDeinit,stopDelay,sceneOrModelToLoad.c_str())==1)
            return(true);
    }
    else
    {
        if (simExtLaunchUIThread("CoppeliaSim",options,sceneOrModelToLoad.c_str(),appDir)==1)
            return(true);
    }
    simAddLog("CoppeliaSimClient",sim_verbosity_errors,"failed launching CoppeliaSim.");
    return(false);
}

THREAD_RET_TYPE simThreadStartAddress(void*)
{
    while (!simExtCanInitSimThread());
    simExtSimThreadInit();
    while (!simExtGetExitRequest())
        simExtStep(true);
    simExtSimThreadDestroy();
    return(THREAD_RET_VAL);
}

int main(int argc,char* argv[])
{
    bool launchAndHandleSimThreadHere=false;
    #ifdef WIN_SIM
        timeBeginPeriod(1);
    #endif
    std::string appDir;
    int res=loadSimLib(argv[0],appDir);
    int exitCode=255;
    if (res==1)
    {
        exitCode=254;
        if (!launchAndHandleSimThreadHere)
        {
            if (run(argc,argv,appDir.c_str(),false))
                exitCode=0;
        }
        else
        {
            #ifdef WIN_SIM
                _beginthread(simThreadStartAddress,0,0);
            #else
                pthread_t th;
                pthread_create(&th,nullptr,simThreadStartAddress,nullptr);
            #endif
            if (run(argc,argv,appDir.c_str(),true))
                exitCode=0;
        }
        if (exitCode==0)
            simGetInt32Parameter(sim_intparam_exitcode,&exitCode);
    }
    if (res>=0)
        unloadSimLib();
    #ifdef WIN_SIM
        timeEndPeriod(1);
    #endif
    //if (exitCode<0)
    //    getchar();
    return(exitCode);
}
