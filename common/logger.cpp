#include "logger.h"

/* Initialize LOGGER */
LOGGER *logger_init(char *logfile, int size, int hour, int check, int level)
{
    //fprintf(stdout, "Initializing logger:%s\n", logfile);
    LOGGER *logger = (LOGGER *)calloc(1, sizeof(LOGGER));
    if(logger)
    {
        logger->add     = logger_add;
        logger->close   = logger_close;
        logger->printf  = logger_printf;
        logger->check   = logger_check;
        
        logger->size = size*1000*1000;
        logger->hour = hour;
        logger->limit = check;
		logger->level = level;
        
        if(logfile)
        {
            strcpy(logger->file, logfile);
            logger->mutex = calloc(1, sizeof(pthread_mutex_t));
            if(logger->mutex) pthread_mutex_init((pthread_mutex_t *)logger->mutex, NULL);


            if( (logger->fd = open(logger->file, O_CREAT |O_WRONLY |O_APPEND, 0644)) <= 0 )
            {
                fprintf(stderr, "FATAL:open log file[%s]  failed, %s",
                        logfile, strerror(errno));
                //logger->close(&logger);
                logger->fd = 1;
            }
        }
        else
        {
            logger->fd = 1;
        }

        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        int ret;

        if ((ret=pthread_create(&thread, &attr, logger->check, (void *)logger)) != 0) {
            fprintf(stderr, "Can't create thread: %s\n", strerror(ret));
            exit(1);
        }
    }
    return logger;
}

/* Add log */
void logger_add(LOGGER *logger, char *__file__, int __line__, int __level__, char *format, ...)
{ 
    va_list ap;
    char buf[LOGGER_LINE_LIMIT]; 
    char *s = buf;
    struct timeval tv; 
    time_t timep; 
    struct tm *p = NULL; 
    int n = 0; 

    if(logger->level > __level__)
	{
	    return;
	}
	
    if(logger) 
    { 
        if(logger->mutex) pthread_mutex_lock((pthread_mutex_t *)logger->mutex); 
        if(logger->fd) 
        { 
            gettimeofday(&tv, NULL); 
            time(&timep); 
            p = localtime(&timep);

            n = sprintf(s,
                    "%04d-%02d-%02d %02d:%02d:%02d +%03ld [%s] [%08x][%s::%d]>> ",
                    (1900+p->tm_year), (1+p->tm_mon), p->tm_mday, p->tm_hour,
                    p->tm_min, p->tm_sec, (size_t)tv.tv_usec/1000, _logger_level_s[__level__],
                    (size_t)pthread_self(), __file__, __line__ );
            s += n;
            va_start(ap, format);
            n = vsprintf(s, format, ap);
            va_end(ap);
            s += n;
            n = sprintf(s, "\n");
            s += n;
            n = s - buf;
            if(write(logger->fd, buf, n) != n) 
            { 
                fprintf(stderr, "FATAL:Writting LOGGER failed, %s", strerror(errno)); 
                close(logger->fd); 
                logger->fd = 0; 
            } 
        }
        if(logger->mutex) pthread_mutex_unlock((pthread_mutex_t *)logger->mutex); 
    }    
}

/* printf log */
void logger_printf(LOGGER *logger, char *format, ...)
{ 
    va_list ap;
    char buf[LOGGER_LINE_LIMIT]; 
    int n = 0; 
    if(logger) 
    { 
        if(logger->mutex) pthread_mutex_lock((pthread_mutex_t *)logger->mutex); 
        if(logger->fd) 
        { 
            va_start(ap, format);
            n = vsprintf(buf, format, ap);
            va_end(ap);
            if(write(logger->fd, buf, n) != n) 
            { 
                fprintf(stderr, "FATAL:Writting LOGGER failed, %s", strerror(errno)); 
                close(logger->fd); 
                logger->fd = 0; 
            } 
        }    

        if(logger->mutex) pthread_mutex_unlock((pthread_mutex_t *)logger->mutex); 
    }    
}

/* Close logger */
void logger_close(LOGGER **logger)
{ 
    if(*logger) 
    { 
        if((*logger)->fd > 0 ) close((*logger)->fd); 
        if((*logger)->mutex)
        {
                    pthread_mutex_unlock((pthread_mutex_t *) (*logger)->mutex); 
                    pthread_mutex_destroy((pthread_mutex_t *) (*logger)->mutex); 
            free((*logger)->mutex);
        }
        free((*logger)); 
        (*logger) = NULL; 
    } 
}

void* logger_check(void* arg)
{
    LOGGER* logger = (LOGGER *)arg;
    struct stat file_stats;
    char szOldFileName[LOGGER_FILENAME_LIMIT + 1] = { 0 };
    struct timeval tv; 
    time_t timep; 
    struct tm *p = NULL; 

    int status;
    status = pthread_detach( pthread_self() );

    while( 1 )
    {
        sleep(logger->limit);
        
        gettimeofday(&tv, NULL); 
        time(&timep); 
        p = localtime(&timep); 
        memset(szOldFileName, 0x00, sizeof(szOldFileName));
        sprintf(szOldFileName, "%s.%04d%02d%02d%02d", 
        logger->file, (1900+p->tm_year), (1+p->tm_mon), p->tm_mday,
        p->tm_hour);
        memset(&file_stats, 0x00, sizeof(struct stat));     
        if ( 0 == stat(logger->file, &file_stats) )
        {
            if ( logger->size <= file_stats.st_size )
            {
            if(logger->mutex) pthread_mutex_lock((pthread_mutex_t *)logger->mutex); 
                remove(szOldFileName);
                rename(logger->file, szOldFileName);
                close(logger->fd);
                if( (logger->fd = open(logger->file, O_CREAT |O_WRONLY |O_APPEND, 0644)) <= 0 )
                {
                    fprintf(stderr, "FATAL:open log file[%s]  failed, %s",
                            logger->file, strerror(errno));
                    //logger->close(&logger);
                    logger->fd = 1;
                }
            if(logger->mutex) pthread_mutex_unlock((pthread_mutex_t *)logger->mutex); 
                continue;
            }
        }
        
        if ( logger->hour == p->tm_hour )
        {
            if ( 0 != access(szOldFileName, F_OK) )
            {
            if(logger->mutex) pthread_mutex_lock((pthread_mutex_t *)logger->mutex); 
                rename(logger->file, szOldFileName);
                close(logger->fd);
                if( (logger->fd = open(logger->file, O_CREAT |O_WRONLY |O_APPEND, 0644)) <= 0 )
                {
                    fprintf(stderr, "FATAL:open log file[%s]  failed, %s",
                            logger->file, strerror(errno));
                    //logger->close(&logger);
                    logger->fd = 1;
                }
            if(logger->mutex) pthread_mutex_unlock((pthread_mutex_t *)logger->mutex); 
            }

            continue;
        }

    }

    pthread_exit( (void *)NULL );
    return NULL;
    
}


