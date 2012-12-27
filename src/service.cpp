#include "service.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "tools.h"

using namespace std;


/* compose error packect*/
void compose_error_resp(char* _out, int& _outlen, sint _errno)
{
    char* p = _out;

    if ( NULL == p )
    {
        return;
    }

    _outlen += Len4MsgHead;

	//if ( 0 > _errno )
	//{
	//    _errno = OTHER_ERROR;
	//}

    //body
    _errno = htons(_errno);
    memcpy(p + _outlen, &_errno, 2);
    _outlen += 2;   
    return;
}

/* handler */
void service_handler(conn *c, const char* _in, int _inlen, char* _out, int& _outlen)
{
    if ( NULL == _in || NULL == _out)
    {
        // error
        return;
    }

    st_head_req *ptrPkgHeadReq= (st_head_req *)_in;

    //消息头字段 转换字节序
    ptrPkgHeadReq->nClientId = ntohl(ptrPkgHeadReq->nClientId);
    ptrPkgHeadReq->nExtClientId = ntohl(ptrPkgHeadReq->nExtClientId);
    ptrPkgHeadReq->nUmsSeq = ntohl(ptrPkgHeadReq->nUmsSeq);
    ptrPkgHeadReq->nMsgType = ntohl(ptrPkgHeadReq->nMsgType);
    ptrPkgHeadReq->nSequence = ntohl(ptrPkgHeadReq->nSequence);
    ptrPkgHeadReq->nLen = ntohl(ptrPkgHeadReq->nLen);

    //日志打印
    DEBUG_LOGGER(dsmplog, "req pkg--->msgT=%d|len=%d|uSeq=%d|cId=%d|xCid=%d|seq=%d",
            ptrPkgHeadReq->nMsgType, _inlen, ptrPkgHeadReq->nUmsSeq,
            ptrPkgHeadReq->nClientId, ptrPkgHeadReq->nExtClientId, ptrPkgHeadReq->nSequence );
    debug_hex_log(_in, _inlen);

    if ( (_inlen < Len4MsgHead) || ((_inlen - Len4MsgHead) !=  ptrPkgHeadReq->nLen) )
    {
        // error
        ERROR_LOGGER(dsmplog, "nLen1 = [%d], nLen2 = [%d]\r\n", _inlen - Len4MsgHead, ptrPkgHeadReq->nLen);
        compose_error_resp(_out, _outlen, INVALID_QUEST_ERROR);
    }
    else
    {
        switch( ptrPkgHeadReq->nMsgType )
        {
			case MACQueryName:      //3005  别名查询
                name_query(c, _in, _inlen, _out, _outlen);
                break;

            default:
                // error
                ERROR_LOGGER(dsmplog, "Invalid nMsgType:%d", ptrPkgHeadReq->nMsgType);
                compose_error_resp(_out, _outlen, OP_TYPE_ERROR);
                break;
        }
    }

    //应答包包头处理
    memcpy(_out, _in, sizeof(st_head_req));
    st_head_resp* ptrPkgHeadAck = (st_head_resp*)_out;

    //包体长度计算
    ptrPkgHeadAck->nLen = _outlen - Len4MsgHead;

    //日志打印
    DEBUG_LOGGER(dsmplog, "ack pkg ===> msgT=%d|len=%d|uSeq=%d|seq=%d",
        ptrPkgHeadAck->nMsgType, _outlen, ptrPkgHeadAck->nUmsSeq, ptrPkgHeadAck->nSequence );
    debug_hex_log(_out, _outlen);
    
    //消息头字段 转换字节序
    ptrPkgHeadAck->nClientId = htonl(ptrPkgHeadAck->nClientId);
    ptrPkgHeadAck->nExtClientId = htonl(ptrPkgHeadAck->nExtClientId);
    ptrPkgHeadAck->nUmsSeq = htonl(ptrPkgHeadAck->nUmsSeq);
    ptrPkgHeadAck->nMsgType = htonl(ptrPkgHeadAck->nMsgType);
    ptrPkgHeadAck->nSequence = htonl(ptrPkgHeadAck->nSequence);
    ptrPkgHeadAck->nLen = htonl(ptrPkgHeadAck->nLen);

    return;
}

/**
 *@brief 计算花费的时间，单位 毫秒
 *@input ptrTmStart 起始时间
 *@return 花费的时间 单位 毫秒
 */
int ElapseTime(struct timeval *ptrTmStart)
{
    struct timeval tmEnd;
    gettimeofday(&tmEnd,NULL);

    return (tmEnd.tv_sec - ptrTmStart->tv_sec)*1000+  (tmEnd.tv_usec - ptrTmStart->tv_usec)/1000;
}


//3005  别名查询
void name_query(conn *c, const char* _in, int _inlen, char* _out, int& _outlen)
{
    // parse message 
    int nRet = 0;
	struct timeval t_start;
	gettimeofday(&t_start,NULL);
	st_head_req stHead;
    memcpy(&stHead, _in, sizeof(st_head_req));
	stHead.nClientId = ntohl(stHead.nClientId);
	stHead.nExtClientId = ntohl(stHead.nExtClientId);
    st_name_query_req stReq;
	memset(&stReq, 0x00, sizeof(st_name_query_req));
    nRet = parse_name_query(_in, stReq);
    if ( 0 != nRet )
    {
        compose_error_resp(_out, _outlen, INVALID_QUEST_ERROR);
		ERROR_LOGGER(dsmplog, "3005 parse error|sUserNumber=%s", stReq.sUserNumber);
        return;
    }

	st_name_query_resp stResp;
	memset(&stResp, 0x00, sizeof(st_name_query_resp));

	INFO_LOGGER(dsmplog, "3005 ok|reqUNum=%s|nOpType=%d|ackUNum=%s|"
	        "sAlias=%s|nClientId=%d|nExtClientId=%d",
	        stReq.sUserNumber, stReq.nOpType, stResp.sUserNumber,
	        stResp.sAlias, stHead.nClientId, stHead.nExtClientId);
	compose_name_query(_out, _outlen, stResp);

}

int parse_name_query(const char* _in, st_name_query_req& _req)
{
    char* p = (char*)_in + Len4MsgHead;

    strncpy(_req.sUserNumber, p, Len4UserNumber);
	p = p + strlen(_req.sUserNumber) + 1;
    _req.nOpType = *p;
    p++;
    
    return 0;
}

void compose_name_query(char* _out, int& _outlen, st_name_query_resp& _resp)
{
    char* p = _out;

    if ( NULL == p )
    {
        return;
    }

    _outlen += Len4MsgHead;

    //body
    _resp.nResult = htons(_resp.nResult);
    memcpy(p + _outlen, &_resp.nResult, 2);
    _outlen += 2;
    strncpy(p + _outlen, _resp.sUserNumber, Len4Mobile);
    _outlen += ( strlen(_resp.sUserNumber) < Len4Mobile ?  strlen(_resp.sUserNumber) : Len4Mobile );
    _outlen++;
	 strncpy(p + _outlen, _resp.sAlias, Len4b256);
    _outlen += ( strlen(_resp.sAlias) < Len4b256 ?  strlen(_resp.sAlias) : Len4b256 );
    _outlen++;
	
    return ;
}

