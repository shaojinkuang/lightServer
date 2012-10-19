#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#ifndef _LOGGER_H
#define _LOGGER_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _TYPEDEF_LOGGER
#define _TYPEDEF_LOGGER
#define LOGGER_FILENAME_LIMIT  	1024
#define LOGGER_LINE_LIMIT  	8192
#define __DEBUG__		1
#define __INFO__		2
#define	__WARN__ 		3
#define	__ERROR__ 		4
#define	__FATAL__ 		5
static char *_logger_level_s[] = {"NO", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
static char *ymonths[]= {
        "Jan", "Feb", "Mar",
        "Apr", "May", "Jun",
        "Jul", "Aug", "Sep",
        "Oct", "Nov", "Dec"};
typedef struct _LOGGER
{
	char file[LOGGER_FILENAME_LIMIT];
	int fd ;
	void *mutex;
	unsigned long size;
	int hour;
	int limit;
	int level;

	void (*add)(struct _LOGGER *, char *, int, int, char *format, ...);		
	void (*close)(struct _LOGGER **);
	void (*printf)(struct _LOGGER *, char *format, ...);
	void* (*check)(void *);
}LOGGER;
/* Initialize LOGGER */
LOGGER *logger_init(char *logfile, int size, int hour, int check, int level);
#endif

/* Add log */
void logger_add(LOGGER *, char *, int, int, char *format,...);

/* Printf log */
void logger_printf(LOGGER *, char *format,...);

/* Check log */
void* logger_check(void *);

/* Close log */
void logger_close(LOGGER **);
#define DEBUG_LOGGER(log, format...)if(log){log->add(log, __FILE__, __LINE__, __DEBUG__,format);}
#define INFO_LOGGER(log, format...)if(log){log->add(log, __FILE__, __LINE__, __INFO__,format);}
#define WARN_LOGGER(log, format...)if(log){log->add(log, __FILE__, __LINE__, __WARN__,format);}
#define ERROR_LOGGER(log, format...)if(log){log->add(log, __FILE__, __LINE__, __ERROR__,format);}
#define FATAL_LOGGER(log, format...)if(log){log->add(log, __FILE__, __LINE__, __FATAL__,format);}
#define PRINTF_LOGGER(log, format...)if(log){log->printf(log,format);}

#ifdef __cplusplus
 }
#endif
#endif
