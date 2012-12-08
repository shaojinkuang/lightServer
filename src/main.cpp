/*
 * main.cpp
 *
 *  Created on: 2012-12-8
 *      Author: Administrator
 */

#include <sys/resource.h>
#include <signal.h>

#include "event.h"
#include "my_struct.h"
//#include "server.h"
#include "Config.h"
#include "logger.h"

struct settings settings;
LOGGER* dsmplog;

static void settings_init(void) {
    settings.access=0700;
    settings.port = 1314;
    settings.maxbytes = 64 * 1024 * 1024; /* default is 64MB */
    settings.maxconns = 1024;         /* to limit connections-related memory to about 5MB */
    settings.verbose = 1;
    settings.oldest_live = 0;
    settings.evict_to_free = 1;       /* push old items out of cache when memory runs out */
    settings.socketpath = NULL;       /* by default, not using a unix socket */
    settings.managed = false;
    settings.factor = 1.25;
    settings.chunk_size = 48;         /* space for a modest key and value */

    settings.num_threads = 10;

    settings.prefix_delimiter = ':';
    settings.detail_enabled = 0;

}

//static conn *listen_conn = NULL;
static struct event_base *main_base;		//主线程中 event_base 的实例指针

static void sig_handler(const int sig) {
    printf("SIGINT handled.\n");

    exit(EXIT_SUCCESS);
}


static int get_cfg(const char * strDomainName,  const char * strFieldName, char * strFieldResult, int nLen)
{
   return Config::instance()->ReadField(strDomainName,strFieldName,nLen,strFieldResult);
}



static int read_cfg()
{
    int nRet = 0;
    char sCfgFile[128 + 1] = { 0 };
	char tmp[Len4b32 + 1] = { 0 };

    strcpy(sCfgFile, "../conf/lightServer.cfg");
	if ( 0 != access(sCfgFile, F_OK) )
    {
		perror( "cfg file not found");
        return -1;
    }

    if (Config::instance()->Load(sCfgFile) != 0)
	{
	   printf("load Config failed!");
	   return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("System", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.port = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("System", "ThreadNum", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.num_threads = atoi(tmp);

	nRet = get_cfg("Log", "Path", settings.sLogPath, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Log", "Maxsize", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nLogSize = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Log", "Hour", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nLogHour = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Log", "Check", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nLogCheck = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Log", "Level", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nLogLevel = atoi(tmp);

	if ( settings.nLogLevel < 1 || settings.nLogLevel > 5 )
	{
	    settings.nLogLevel = 1;
	}

    return 0;
}



int main (int argc, char **argv) {
//    int c;
//    int x;
    //bool daemonize = true;
//    bool preallocate = false;
//    int maxcore = 1;
    struct rlimit rlim;

    /* listening socket */
    static int *l_socket = NULL;

    /* handle SIGINT */
    signal( SIGQUIT , SIG_IGN );
    signal( SIGPIPE , SIG_IGN );
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, sig_handler);

    /* init settings */
    settings_init();

    /* read cfg*/
	if ( 0 != read_cfg() )
	{
	    fprintf(stderr, "failed to read Config file\n");
	    return 1;
	}

    /*
     * If needed, increase rlimits to allow as many connections
     * as needed.
     */
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        fprintf(stderr, "failed to getrlimit number of files\n");
        exit(EXIT_FAILURE);
    } else {
        int maxfiles = settings.maxconns;
        if (rlim.rlim_cur < maxfiles)
            rlim.rlim_cur = maxfiles + 3;
        if (rlim.rlim_max < rlim.rlim_cur)
            rlim.rlim_max = rlim.rlim_cur;
        if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
            fprintf(stderr, "failed to set rlimit for open files. Try running as root or requesting smaller maxconns value.\n");
            exit(EXIT_FAILURE);
        }
    }

    /* initialize main thread libevent instance */
    main_base = (event_base*)event_init();

    //free conn list 初始化
//    conn_init();

    //初始化工作线程
//    thread_init(settings.num_threads, main_base);

//    /* create server socket */
//	if (server_socket(settings.port, 0)) {
//            fprintf(stderr, "failed to listen\n");
//            exit(EXIT_FAILURE);
//    }

	//初始化日志
	dsmplog = logger_init(settings.sLogPath, settings.nLogSize,
	    settings.nLogHour, settings.nLogCheck, settings.nLogLevel);

	INFO_LOGGER(dsmplog, "init log success");

	int iRet = 0;

	//进入 event loop
    event_base_loop(main_base, 0);

    //释放资源
    if (l_socket)
      free(l_socket);

    logger_close(&dsmplog);

    return 0;
}

