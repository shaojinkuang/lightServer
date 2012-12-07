/*
 * main.cpp
 *
 *  Created on: 2012-12-8
 *      Author: Administrator
 */


#include "Config.h"
#include "logger.h"

static void settings_init(void);

static void settings_init(void) {
    settings.access=0700;
    settings.port = 1314;
    settings.udpport = 0;
    /* By default this string should be NULL for getaddrinfo() */
    settings.inter = NULL;
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

    settings.nActiveMQ = 0;
    settings.nRetry = 0;
}

struct settings settings;
LOGGER* dsmplog;

static conn *listen_conn = NULL;
static struct event_base *main_base;

/*
 * Free list management for connections.
 */

static conn **freeconns;
static int freetotal;
static int freecurr;


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
	char* pHomePath = NULL;
    char sCfgFile[128 + 1] = { 0 };
	char tmp[Len4b32 + 1] = { 0 };

    pHomePath = getenv( "HOME" );
    if ( NULL == pHomePath )
    {
    	// 当前目录
    	strcpy(sCfgFile, "./dsmp.cfg");
    }
    else
    {
        // 获取配置文件名
        snprintf(sCfgFile, sizeof(sCfgFile),"%s/dsmp/cfg/dsmp.cfg", pHomePath);
		if ( 0 != access(sCfgFile, F_OK) )
	    {
	       strcpy(sCfgFile, "dsmp.cfg");
	    }
    }

	if ( 0 != access(sCfgFile, F_OK) )
    {
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
	nRet = get_cfg("System", "Daemon", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.daemonize = atoi(tmp);

	nRet = get_cfg("System", "Username", settings.sUserName, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("System", "Password", settings.sPwd, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("System", "Database", settings.sDataBase, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("System", "ThreadNum", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.num_threads = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("System", "PartId", tmp, Len4b32);
	if ( 0 != nRet )
	{
		return -1;
	}

	settings.nPartId= atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Database", "DbNum", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.num_db = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Database", "MaxSize", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.max_db = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Database", "Interval", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.interval_db = atoi(tmp);

	nRet = get_cfg("PubDB", "Database", settings.sPubDataBase, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("PubDB", "Username", settings.sPubUserName, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("PubDB", "Password", settings.sPubPwd, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("PubDB", "MaxSize", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.max_pub_db = atoi(tmp);

	nRet = get_cfg("SSI", "Host", settings.sSSIHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("SSI", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nSSIPort = atoi(tmp);

    nRet = get_cfg("SSI", "SrvInfo", settings.sSrvInfo, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("PushMail", "Host", settings.sPMailHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("PushMail", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nPMailPort = atoi(tmp);

    nRet = get_cfg("PushMail", "SrvInfo", settings.sPMailSrvInfo, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("Fetion", "Host", settings.sFetionHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Fetion", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nFetionPort = atoi(tmp);

    nRet = get_cfg("Fetion", "SrvInfo", settings.sFetionSrvInfo, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("Message", "Path", settings.sMsgPath, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("Message", "Host", settings.sMsgHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("Message", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nMsgPort = atoi(tmp);

	nRet = get_cfg("MessageOp", "Path", settings.sMsgOpPath, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("MessageOp", "Host", settings.sMsgOpHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("MessageOp", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nMsgOpPort = atoi(tmp);

    nRet = get_cfg("CallBack", "Host", settings.sCBHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CallBack", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nCBPort = atoi(tmp);

	nRet = get_cfg("CMail", "Host", settings.sCMailHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CMail", "Port", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nCMailPort = atoi(tmp);

	nRet = get_cfg("CMail", "Domain", settings.sDomain, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CMail", "RenRenIndex", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
    settings.nRenRenIndex = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CMail", "ActivtyNo", tmp, Len4b32);
	if ( 0 != nRet )
	{
	    return -1;
	}
    settings.nActivtyNo = atoi(tmp);

	//wxj add ,for open to other carriers
	nRet = get_cfg("CMail", "SpsidForOtherCarrier", settings.sSpsidForOtherCarrier, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}
	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("CMail", "IndexForOtherCarrier", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nIndexForOtherCarrier = atoi(tmp);

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("CMail", "BasicMailSystemType", tmp, Len4b32);
    if(0 != nRet)
    {
        return -1;
    }
    settings.nBasicMailSystemType = atoi(tmp);

    nRet = get_cfg("RMailAppendOp", "2002AppendOp", settings.s2002AppendOp, Len4b128);
    if(0 != nRet)
    {
        return -1;
    }

    fprintf( stderr, "\n%s Line.%d--> 111 settings.s2002AppendOp[%s] \n", __FILE__, __LINE__, settings.s2002AppendOp);

    // activemq
	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("ActiveMQ", "Open", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nActiveMQ = atoi(tmp);

	nRet = get_cfg("ActiveMQ", "BrokerUrl", settings.sBrokerUrl, Len4b256);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("ActiveMQ", "Destination", settings.sDestination, Len4b128);
	if ( 0 != nRet )
	{
	    return -1;
    }

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("ActiveMQ", "Retry", tmp, Len4b32);
	if ( 0 != nRet )
	{
        return -1;
    }
    settings.nRetry = atoi(tmp);

	//nRet = get_cfg(sCfgFile, "ActiveMQ", "DynamicZoneType", settings.sDynamicZoneType, Len4b32);
	//if ( 0 != nRet )
	//{
    //    return -1;
    //}

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

	//WebServer
	nRet = get_cfg("WebServer", "Host", settings.sWebHost, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	nRet = get_cfg("WebServer", "path", settings.sWebPath, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}

	memset(tmp, 0x00, sizeof(tmp));
	nRet = get_cfg("WebServer", "Port", tmp, Len4b64);
	if ( 0 != nRet )
	{
	    return -1;
	}
	settings.nWebPort = atoi(tmp);

	memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("WebServer", "SendFlag", tmp, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }
    settings.nSendFlag = atoi(tmp);


    //WapClean
    nRet = get_cfg("WapClean", "Host", settings.sWapCleanHost, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }

    nRet = get_cfg("WapClean", "path", settings.sWapCleanPath, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("WapClean", "Port", tmp, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }
    settings.nWapCleanPort = atoi(tmp);

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("WapClean", "SendFlag", tmp, Len4b64);
    if ( 0 != nRet )
    {
        return -1;
    }
    settings.nWapCleanSendFlag = atoi(tmp);



    //ud router memcache 连接池
    nRet = get_cfg("Memcache", "libmemcached_pool_server", settings.sMemcachePoolServer, Len4b128);
    if(0 != nRet)
    {
        return -1;
    }

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("Memcache", "libmemcached_pool_size", tmp, Len4b64);
    if(0 != nRet)
    {
        return -1;
    }
    settings.nMemcachePoolSize = atoi(tmp);

    fprintf( stderr, "\n%s Line.%d--> settings.sMemcachePoolServer[%s], nMemcachePoolSize[%d] \n", __FILE__, __LINE__, settings.sMemcachePoolServer, settings.nMemcachePoolSize );

    //PS memcache 连接池
    nRet = get_cfg("PSMemcache", "ps_libmemcached_pool_server", settings.sPSMemcachePoolServer, Len4b128);
    if(0 != nRet)
    {
        return -1;
    }

    memset(tmp, 0x00, sizeof(tmp));
    nRet = get_cfg("PSMemcache", "ps_libmemcached_pool_size", tmp, Len4b64);
    if(0 != nRet)
    {
        return -1;
    }
    settings.nPSMemcachePoolSize = atoi(tmp);

    fprintf( stderr, "\n%s Line.%d--> settings.sPSMemcachePoolServer[%s], nPSMemcachePoolSize[%d] \n", __FILE__, __LINE__, settings.sPSMemcachePoolServer, settings.nPSMemcachePoolSize );


    return 0;
}



int main (int argc, char **argv) {
    int c;
    int x;
    //bool daemonize = true;
    bool preallocate = false;
    int maxcore = 1;
    char *username = NULL;
    char *pid_file = NULL;
    struct passwd *pw;
    struct sigaction sa;
    struct rlimit rlim;
    /* listening socket */
    static int *l_socket = NULL;

    /* udp socket */
    static int *u_socket = NULL;
    static int u_socket_count = 0;

    /* handle SIGINT */
    signal( SIGQUIT , SIG_IGN );
    signal( SIGPIPE , SIG_IGN );
    signal(SIGINT, sig_handler);
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

    /* lose root privileges if we have them */
    if (getuid() == 0 || geteuid() == 0) {
        if (username == 0 || *username == '\0') {
            fprintf(stderr, "can't run as root without the -u switch\n");
            return 1;
        }
        if ((pw = getpwnam(username)) == 0) {
            fprintf(stderr, "can't find the user %s to switch to\n", username);
            return 1;
        }
        if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
            fprintf(stderr, "failed to assume identity of user %s\n", username);
            return 1;
        }
    }

    /* initialize main thread libevent instance */
    main_base = (event_base*)event_init();

    /* initialize other stuff */
    conn_init();
    suffix_init();

    /*
     * ignore SIGPIPE signals; we can use errno==EPIPE if we
     * need that information
     */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1 ||
        sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("failed to ignore SIGPIPE; sigaction");
        exit(EXIT_FAILURE);
    }

    /* start up worker threads if MT mode */
    thread_init(settings.num_threads, main_base);

    /* save the PID in if we're a daemon, do this after thread_init due to
       a file descriptor handling bug somewhere in libevent */
    if (settings.daemonize == 1)
        save_pid(getpid(), pid_file);

    /* initialise clock event */
    clock_handler(0, 0, 0);

    /* create server socket */
	if (server_socket(settings.port, 0)) {
            fprintf(stderr, "failed to listen\n");
            exit(EXIT_FAILURE);
    }

    /* init log*/
	dsmplog = logger_init(settings.sLogPath, settings.nLogSize,
	    settings.nLogHour, settings.nLogCheck, settings.nLogLevel);

	INFO_LOGGER(dsmplog, "init log success");

	int iRet = 0;
    //profile server memcache集群连接池初始化

    /* enter the event loop */
    event_base_loop(main_base, 0);
    /* remove the PID file if we're a daemon */
    if (settings.daemonize == 1)
        remove_pidfile(pid_file);
    /* Clean up strdup() call for bind() address */
    if (settings.inter)
      free(settings.inter);
    if (l_socket)
      free(l_socket);
    if (u_socket)
      free(u_socket);

	logger_close(&dsmplog);

    return 0;
}

