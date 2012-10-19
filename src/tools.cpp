/*
 * tools.cpp
 *
 *  Created on: 2012-2-1
 *      Author: Administrator
 */

#include "tools.h"
#include "pkg.h"
#include "logger.h"

#include <openssl/md5.h>

/**
 *@brief 记录二进制格式的  debug 级别日志
 */
void debug_hex_log(const char* _buf, int _len)
{
    //for (int i=0; i < _len; i++)
    //{

    //    if (0 == i % 16)
    //    {
    //        PRINTF_LOGGER(dsmplog,"\n%d\t",i);
    //    }
    //    PRINTF_LOGGER(dsmplog, "%02X ", (unsigned char)_buf[i] & 0xFF);
    //}
    //PRINTF_LOGGER(dsmplog, "\n");
    int j=0;
    char b[1024]={0};
    for (int i=0; i<_len; i++) {
        if (i % 16 == 0) {
            sprintf(b+3*i+j, "\n%02X ", (unsigned char)_buf[i] & 0xFF);
            j++;
        }
        else {
            sprintf(b+3*i+j, "%02X ", (unsigned char)_buf[i] & 0xFF);
        }
    }
    DEBUG_LOGGER(dsmplog, "%s", b);
}

/**
 *@brief md5加密
 *@param src 入参,需要加密的字符串
 *@param des 出参,加密后的字符串,长度为33字节(含 '\0')
 *@param len 入参,des 的长度
 */
void md5encrypt( const char* src, char* des, int len )
{
    //密码加密
    MD5_CTX ctx; //md5 context
    unsigned char md[16] = { 0 }; //加密后数据，二进制格式
    char buff[33] = { 0 }; //加密后数据，字符串格式
    char tmp[3] = { 0 };

    //二进制加密
    MD5_Init(&ctx);
    MD5_Update(&ctx, src, strlen(src));
    MD5_Final(md, &ctx);

    //加密数据转为字符串格式,字母为小写
    for(int i = 0; i < 16; i++)
    {
        sprintf(tmp, "%02x", md[i]);
        strcat(buff, tmp);
    }

    //拷贝字符
    int iCpyLen = (len > sizeof(buff)? sizeof(buff) : len );
    memcpy( des, buff, iCpyLen );
}



/**
 *@brief 字符串转小写
 *@param input ptrSrc 字符指针
 *@return 转小写后的字符串
 */
std::string Lower(const char* ptrSrc)
{
    std::string lower(ptrSrc);
    for(size_t i = 0; i < lower.size(); i++)
        lower[i] = tolower(lower[i]);

    return lower;
}

/**
 *@brief 如果是手机号，去掉"86"前缀
 *  判断依据：入参是13位数字,且 前3位是"861"
 *@param input strSrc  ： userNumber 有可能为 手机号或者别名， 函数处理中不涉及对该入参的修改
 *@return  返回的 userNumber ，已经去了 "86"前缀
 */
std::string Trim86(const std::string& strSrc)
{
    std::string strRet(strSrc);

    if( 13 == strRet.size() &&
        IsAllDigit(strRet.c_str()) &&
        0 == strncmp(strRet.c_str(), "861", 3) )
    {
        strRet.erase(0, 2);
    }

    return strRet;
}

/**
 *@brief 所有字符是否都是数字
 *@return 0 否         非0  是
 */
int IsAllDigit(const char* ptrSrc)
{
    int iRetVal = 1;
    char * ptrCurr = (char*)ptrSrc;

    //是否有非数字的字母
    while(*ptrCurr)
    {
        if(0 == isdigit(*ptrCurr))
        {
            iRetVal = 0;
            break;
        }
        ptrCurr++;
    }

    return iRetVal;
}


