/*
 * tools.h
 *
 *  Created on: 2012-2-1
 *      Author: Administrator
 */

#ifndef TOOLS_H_
#define TOOLS_H_

#include <string>

void debug_hex_log(const char* _buf, int _len);

void md5encrypt( const char* src, char* des, int len );

std::string Lower(const char* ptrSrc);

std::string Trim86(const std::string& strSrc);

int IsAllDigit(const char* ptrSrc);


#endif
