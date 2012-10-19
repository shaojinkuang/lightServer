#ifndef _DSMP_SERVICE_H
#define _DSMP_SERVICE_H
//#include "pkg.h"
#include "dsmp.h"

/* handler */
void service_handler(conn *c, const char* _in, int _inlen, char* _out, int& _outlen);

/* compose error packect*/
void compose_error_resp(char* _out, int& _outlen, sint _errno);


//3005  别名查询
void name_query(conn *c, const char* _in, int _inlen, char* _out, int& _outlen);
int parse_name_query(const char* _in, st_name_query_req& _req);
void compose_name_query(char* _out, int& _outlen, st_name_query_resp& _resp);

#endif
