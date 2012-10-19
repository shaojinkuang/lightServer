#ifndef _DSMP_PKG_H
#define _DSMP_PKG_H
#include <sys/types.h>
#include "macro.h"
#include "logger.h" 

/* unistd.h is here */
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

/** Time relative to server start. Smaller than time_t on 64-bit systems. */
typedef unsigned int rel_time_t;

#pragma pack(1)

struct settings {
    size_t maxbytes;
    int maxconns;
    int port;
	int nPartId;
    int udpport;
    char *inter;
    int daemonize;
    int verbose;
    rel_time_t oldest_live; /* ignore existing items older than this */
    bool managed;          /* if 1, a tracker manages virtual buckets */
    int evict_to_free;
    char *socketpath;   /* path to unix socket if using local socket */
    int access;  /* access mask (a la chmod) for unix domain socket */
    double factor;          /* chunk size growth factor */
    int chunk_size;
    int num_threads;        /* number of libevent threads to run */
    int num_db;        /* number of libevent threads to run */
    int max_db;
    int interval_db;
    char prefix_delimiter;  /* character that marks a key prefix (for stats) */
    int detail_enabled;     /* nonzero if we're collecting detailed stats */

	char sUserName[Len4b64 + 1];
	char sPwd[Len4b64 + 1];
	char sDataBase[Len4b64 + 1];

	char sPubUserName[Len4b64 + 1];
	char sPubPwd[Len4b64 + 1];
	char sPubDataBase[Len4b64 + 1];
    //int init_pub_db;
    int max_pub_db;

	char sSSIHost[Len4b64 + 1];
	int  nSSIPort;
	char sSrvInfo[Len4b128 + 1];
	char sPMailHost[Len4b64 + 1];
	int  nPMailPort;
	char sPMailSrvInfo[Len4b128 + 1];
	char sFetionHost[Len4b64 + 1];
	int  nFetionPort;
	char sFetionSrvInfo[Len4b128 + 1];

	char sMsgPath[Len4b64 + 1];
	char sMsgHost[Len4b64 + 1];
	int  nMsgPort;

	char sMsgOpPath[Len4b64 + 1];
	char sMsgOpHost[Len4b64 + 1];
	int  nMsgOpPort;
	//char sFetionSrvInfo[Len4b128 + 1];
	char sCBHost[Len4b64 + 1];
	int  nCBPort;
	char sCMailHost[Len4b64 + 1];
	int  nCMailPort;
    int  nRenRenIndex;
    int  nActivtyNo;
	char sDomain[Len4b128 + 1];
	int  nBasicMailSystemType;      //基础邮箱系统类型 0 CMail   1 RMail

	char s2002AppendOp[Len4b128+1];

    int  nActiveMQ;
    char sBrokerUrl[Len4b256 +1];
    char sDestination[Len4b128 + 1];
    int  nRetry;
    char sDynamicZoneType[Len4b64 + 1];

	char sLogPath[Len4b128 + 1];
	int  nLogSize;
	int  nLogHour;
	int  nLogCheck;
	int  nLogLevel;
	//wxj add 2011-10-14 for unicom and telecom
	//must add "IndexForOtherCarrier = x"  and "SpsidForOtherCarrier = lt06139,dx06139
	
	int nIndexForOtherCarrier;//welcom index for cm/rm
	char sSpsidForOtherCarrier[Len4b64 + 1];
	
	char sWebPath[Len4b64 + 1];
	char sWebHost[Len4b64 + 1];
	int  nWebPort;
	int  nSendFlag;
	
	char sWapCleanPath[Len4b64 + 1];
	char sWapCleanHost[Len4b64 + 1];
	int  nWapCleanPort;
	int  nWapCleanSendFlag;

	//ud router 缓存集群参数
	char sMemcachePoolServer[Len4b128+1];
	int  nMemcachePoolSize;

	//PS 缓存集群参数
	char sPSMemcachePoolServer[Len4b128+1];
	int  nPSMemcachePoolSize;


};

extern struct settings settings;
extern LOGGER* dsmplog;

//文件配置
typedef struct _st_cfg
{
    char sUsername[Len4b128 + 1];
    char sPassword[Len4b128 + 1];
    char sDatabase[Len4b128 + 1];  
} st_cfg;

typedef struct _st_paras
{
    char para1[Len4b128 + 1];
	char para2[Len4b128 + 1];
	char para3[Len4b128 + 1];
	char para4[Len4b128 + 1];
	char para5[Len4b128 + 1];
	char para6[Len4b128 + 1];
	char para7[Len4b128 + 1];
	char para8[Len4b128 + 1];
}st_paras;

// 消息头
typedef struct _st_head_req
{
    int nClientId;                                 // 请求系统的来源，由服务端进行分配
    int nExtClientId;                              // 
    int nUmsSeq;                                   // UmsSeq，由umsRouter取自DB
    int nMsgType;                                  // 消息类型，即原"接口功能代码"，参见附录"消息类型"
    int nSequence;                                 // 协议包的序列号，每个数据包的序列号要求不能重复，短连接可以不需要，长连接则需要
    int nLen;                                      // 包体的长度
}st_head_req;

typedef st_head_req st_head_resp;

//1001 套餐信息查询
typedef struct _st_query_prest_req
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
}st_query_prest_req;

// 应答消息
typedef struct _st_query_prest_resp
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    sint nResult;                                  // 0：操作成功
    char sPackId[Len4b128 + 1];                    // 用户可用套餐信息
    int  nPresentSMS;                              // 赠送短信数
    int  nPresentMMS;                              // 赠送彩信数
    int  nPresentNMS;                              // 赠送到达通知数
    int  nPresentFax;                              // 赠送传真数
    int  nPresentDisk;                             // 赠送网盘容量
    int  nPresentOther;                            // 其它赠送
    int  nExt1;                                    // 保留
    int  nExt2;                                    // 保留
    char sExt3[Len4b256 + 1];                      // 保留
}st_query_prest_resp;


//1002 业务信息查询
typedef struct _st_query_info_req
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    int  nServiceId;                               // 业务大类
}st_query_info_req;

typedef struct _st_query_info_resp
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    int  nServiceId;                               // 业务大类
    sint nResult;                                  // 0：操作成功
    int  nPackId;                                  // 业务所在的套餐
    int  nProvCode;                                // 省份
    int  nAreaCode;                                // 地区
    int  nCardType;                                // 卡品牌
    char sServItem[Len4ServItem + 1];              // 业务代码
    int  nValue;                                   // 容量  单位:M
    int  nLevel;                                   // 等级
    int  nStatus;                                  // 状态
    char sBindDate[Len4Date + 1];                  // 开始计费日期
    char sUnBindDate[Len4Date + 1];                // 捆绑到期日期
    char sLastRegDate[Len4Date + 1];               // 最后一次注册时间
    sint nRegChannel;                              // 注册方式
    char sUnRegDate[Len4Date + 1];                 // 注销时间
    sint nUnRegNum;                                // 注销次数
    sint nUnRegChannel;                            // 注销方式
    char sPauseDate[Len4Date + 1];                 // 暂停时间
    sint nPauseChannel;                            // 暂停方式
    char sResumeDate[Len4Date + 1];                // 恢复时间
    sint nResumeChannel;                           // 恢复方式
    char sComeFrom[Len4b256 + 1];                  // 注册来源，简短说明注册的来源，如个邮等
    int  nBindId;                                  // 活动ID
    int  nRegType;                                 // 注册类型, 0:自主注册 1:后台捆绑
    int  nOPFlg;                                   // 运营使用标志
    char sRemo[Len4b256 + 1];                      // 备注
    int  nExt1;                                    // 保留
    char sExt2[Len4b256 + 1];                      // 保留
}st_query_info_resp;

//1003 套餐基本信息查询
typedef struct _st_query_packet_req
{
    int  nPackId;                                  // 套餐编码
}st_query_packet_req;

typedef struct _st_query_packet_resp
{
    int  nPackId;                                  // 套餐编码
    sint nResult;                                  // 0：操作成功
    char sPackName[Len4b128 + 1];                  // 套餐名称
    int  nPackType;                                // 套餐类型 0:主套餐 1:从套餐 2:衍生套餐
    int  nStatus;                                  // 状态 0:正常 1:暂停
    int  nFeevalue;                                // 金额
    sint nPayType;                                 // 支付方式
    int  nFeeType;                                 // 计费方式
    char sFeeCode[Len4b32 + 1];                    // 全月计费代码
    char sHalfFeeCode[Len4b32 + 1];                // 半月计费代码
    int  nBindId;                                  // 默认活动信息
    int  nPresentSMS;                              // 赠送短信数
    int  nPresentMMS;                              // 赠送彩信数
    int  nPresentNMS;                              // 赠送到达通知数
    int  nPresentFax;                              // 赠送传真数
    int  nPresentDisk;                             // 赠送网盘容量
    int  nPresentOther;                            // 其它赠送
    char sBossCode[Len4b32 + 1];                   // Boss对应套餐编码
    int  nBossFlg;                                 // 是否同步给Boss
    int  nShowFlg;                                 // 是否显示在WEB页面上
    char sPackInfo[Len4TmpString + 1];             // 套餐说明
}st_query_packet_resp;


//2001 注册鉴权
typedef struct _st_auth_req
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    int  nPackId;                                  // 套餐编码
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
}st_auth_req;

typedef struct _st_auth_resp
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    int  nPackId;                                  // 套餐编码
    sint nResult;                                  // 0：操作成功
}st_auth_resp;


//3001 别名操作
typedef struct _st_modify_name_req
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    int  nOpType;                                  // 操作类型　0:新增　1:删除　2:修改
    char sNewAlias[Len4b64 + 1];                   // 新别名 OpType=0,2时必须
    char sOldAlias[Len4b64 + 1];                   // 旧别名 OpType=1,2时必须
    int  nOPMode;                                  // 操作方式，参见附录“操作方式说明”
    int  nComeFrom;                                // 用户来源：0 邮箱别名   1 飞信别名   2 V139别名    3 其他
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
    int  nSendFlg;                                 // 是否发送短信  0发送 1不发送
    char sFetion[Len4b64 + 1];
}st_modify_name_req;

typedef struct _st_modify_name_resp
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    int  nOpType;                                  // 操作类型　0:新增　1:删除　2:修改
    sint nResult;                                  // 0：操作成功
}st_modify_name_resp;

//3005  别名查询
typedef struct _st_name_query_req
{
    char sUserNumber[Len4UserNumber + 1];              // 手机号码
    int  nOpType;
}st_name_query_req;

typedef struct _st_name_query_resp
{
    sint nResult; 
    char sUserNumber[Len4Mobile + 1];              // 手机号码
	char sAlias[Len4b256 + 1];
}st_name_query_resp;

//3007  密码校验
typedef struct _st_check_passwd_req
{
    char sUserNumber[Len4UserNumber + 1];              // 手机号码
    char sPwd[Len4b64 + 1];                             // 密码
}st_check_passwd_req;

typedef struct _st_check_passwd_resp
{
    UInt16 nResult;
    UInt16 nMatch;                    //密码是否匹配   0 否   1 是
}st_check_passwd_resp;


//2010    省份套餐查询
typedef struct _st_mealid_query_req {
    int nProvCode;
    char sServiceId[Len4ServItem + 1];
    char sServiceItem[Len4ServItem + 1];
}st_mealid_query_req;

typedef struct _st_mealid_query_resp {
    sint nResult;
    int nMealId;
}st_mealid_query_resp;

//3002  修改密码
typedef struct _st_modify_pwd_req
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    char sPwd[Len4b64 + 1];                        // 新密码 OpType=0,2时必须
    char sOldPwd[Len4b64 + 1];                     // 用户原密码
    int  nVerifyFlg;                               // 是否需要验证用户密码
    int  nRandomFlg;                               // 是否生成随机密码
    int  nPwdType;                                 // 密码类型 0:明文  1:md5
    int  nOPMode;                                  // 操作方式，参见附录“操作方式说明”
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
    int  nSendFlg;                                 // 是否发送短信  0发送 1不发送
    int  nBindId;                                 // 是否发送短信  0发送 1不发送

    // 存储过程返回
    char sMsg[Len4TmpString + 1];                  // 下发短信语
    char sSpsId[Len4SpsId + 1];                    // 下发短信省份ID
    char sSpNumber[Len4SpNumber + 1];

}st_modify_pwd_req;

typedef struct _st_modify_pwd_resp
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    sint nResult;                                  // 0：操作成功
}_st_modify_pwd_resp;


//3004  号段信息查询
typedef struct _st_query_prefix_req
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
}st_query_prefix_req;

typedef struct _st_query_prefix_resp
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    sint nResult;                                  // 0：操作成功
    int  nProvCode;                                // 省份编号，当Result=0时返回
    char sProvDesc[Len4b64 + 1];                   // 省份名称，当Result=0时返回
    int  nAreaCode;                                // 城市编号，当Result=0时返回
    char sAreaDesc[Len4b64 + 1];                   // 城市名称，当Result=0时返回
    int  nCardType;                                // 品牌，当Result=0时返回
}st_query_prefix_resp;


//4001  个人基础信息查询
typedef struct _st_query_base_req
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
}st_query_base_req;

typedef struct _st_query_base_resp
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    sint nResult;                                  // 0：操作成功
    char sUserMobile[Len4Mobile + 1];              // 手机号码，当Result=0时返回
    int  nStatus;                                  // 用户状态，当Result=0时返回
    int  nMobileStatus;                            // 手机号码状态，当Result=0时返回
    int  nUserType;                                // 用户类型, 当Result=0时返回
    int  nProvCode;                                // 归属省份, 当Result=0时返回
    int  nAreaCode;                                // 城市编号，当Result=0时返回
    int  nCardType;                                // 品牌，当Result=0时返回
}st_query_base_resp;


//4009  个邮用户信息查询专用
typedef struct _st_query_mail_req
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    int  nServiceId;                               // 业务编号
}st_query_mail_req;

typedef struct _st_query_mail_resp
{
    char sUserNumber[Len4UserNumber + 1];          // 手机号码
    int  nServiceId;                               // 业务编号
    sint nResult;                                  // 0：操作成功
    int  nPackId;                                  // 业务所在的套餐
    char sServItem[Len4ServItem + 1];              // 业务代码，当Result=0时返回
    int  nBindId;                                  // 用户活动编号 ，当Result=0时返回
    int  nUserType;                                // 用户类别，当Result=0时返回
    int  nProvCode;                                // 省份，当Result=0时返回
    int  nAreaCode;                                // 地区，当Result=0时返回
    int  nCardType;                                // 品牌，当Result=0时返回
    int  nIntegral;                                // 用户积分系统，当Result=0时返回
    char sLevelDesc[Len4Level + 1];                // 用户等级说明，当Result=0时返回
    char sUserName[Len4UserNumber + 1];            // 用户真实姓名，当Result=0时返回
    char sUserAalias[Len4UserNumber + 1];          // 用户别名，当Result=0时返回
    char sLastLoginDate[Len4Date + 1];             // 最后一次登录时间，当Result=0时返回
    char sLastIP[Len4IP + 1];                      // 最后一次登录IP地址，当Result=0时返回
    char sCurLoginDate[Len4Date + 1];              // 本次登录时间，当Result=0时返回
    char sCurIP[Len4IP + 1];                       // 本次登录IP地址，当Result=0时返回
    char sRemo[Len4b256 + 1];                      // 备注
    int  nExt1;                                    // 保留
    char sExt2[Len4b256 + 1];                      // 保留
}st_query_mail_resp;

//2002      注册
typedef struct _st_packet_register_req
{
    char sUserNumber[Len4Mobile + 1];              // 手机号码
    int  nPackId;                                  // 业务所在的套餐
    char sPwd[Len4b64 + 1];                        // 用户注册套餐时的密码
    int  nEncryptFlg;                              // 控制密码是否已经进行了MD5加密
    int  nOPMode;                                  // 操作方式，参见附录“操作方式说明”
    int  nBindId;                                  // 活动号
    int  nVerifyFlg;                               // 是否需要向BOSS鉴权,0是，1否
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
    int  nSendFlg;                                 // 是否发送短信  0发送 1不发送

    // 存储过程返回值
    int  nServId;                                  // 邮箱服务ID 主要是给网盘回调接口使用
    int  nServItem;                                // 邮箱的子业务ID 主要是给网盘回调接口使用
    int  nNetCosId;                                // 网盘的cosID    主要是给网盘回调接口使用
    char sMsg[Len4TmpString + 1];                  // 下发短信语
    char sSpsId[Len4SpsId + 1];                    // 下发短信省份ID

    char sSendTime[Len4StartSendTime+1];           // 短信下发时间

}st_packet_register_req;

typedef struct _st_packet_register_resp
{
    sint nResult;                                  // 0：操作成功
}st_packet_register_resp;

typedef struct _st_packet_info
{
    int  nPackId;                                  // 业务所在的套餐
    int  nPackType;
	int  nBakPackType;
	char sAlias[Len4b64 + 1];
    char sCosId[Len4ServItem + 1];
	char sBakCosId[Len4ServItem + 1];
	char sOrgId[Len4ServItem + 1];
	char sSerivceId[Len4ServItem + 1];
	int  nRet;
	char sFetion[Len4b64*2 + 1];
}st_packet_info;

//2003    注销
typedef struct _st_packet_unreg_req
{
    char sUserNumber[Len4Mobile + 1];              // 手机号码
    int  nPackId;                                  // 业务所在的套餐
    int  nOPMode;                                  // 操作方式，参见附录“操作方式说明”
    int  nUnRegFlg;                                // 强制注销 : 0 否 1 是
    int  nUnRegType;                               // 取消原因
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
    int  nSendFlg;                                 // 是否发送短信  0发送 1不发送
    int  nVgopSyncFlag;                            // vgop同步 0同步 1不同步

    // 存储过程返回值
    int  nServId;                                  // 邮箱服务ID 主要是给网盘回调接口使用
    int  nServItem;                                // 邮箱的子业务ID 主要是给网盘回调接口使用
    int  nNetCosId;                                // 网盘的cosID    主要是给网盘回调接口使用
    char sMsg[Len4TmpString + 1];                  // 下发短信语
    char sSpsId[Len4SpsId + 1];                    // 下发短信省份ID
    char sSendTime[Len4StartSendTime+1];           // 短信下发时间
    int  nServiceId;                               // serviceId   10为邮箱业务

}st_packet_unreg_req;

typedef struct _st_packet_unreg_resp
{
    sint nResult;                                  // 0：操作成功
}st_packet_unreg_resp;

//2004    变更
typedef struct _st_packet_update_req
{
    char sUserNumber[Len4Mobile + 1];              // 手机号码
    int  nPackId;                                  // 业务所在的套餐
    int  nOPMode;                                  // 操作方式，参见附录“操作方式说明”
    int  nOldOPMode;
    int  nBindId;
    int  nParamisChange;
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
    int  nSendFlg;                                 // 是否发送短信  0发送 1不发送

    // 存储过程返回值
    int  nServId;                                  // 邮箱服务ID 主要是给网盘回调接口使用
    int  nServItem;                                // 邮箱的子业务ID 主要是给网盘回调接口使用
    int  nNetCosId;                                // 网盘的cosID    主要是给网盘回调接口使用
    char sMsg[Len4TmpString + 1];                  // 下发短信语
    char sSpsId[Len4SpsId + 1];                    // 下发短信省份ID

    int nCoreMailSynFlag;                          // 是否需要同步coremail
    int nVgopSynFlag;                              // 是否需要同步vgop

    char sSendTime[Len4StartSendTime+1];           // 短信下发时间
}st_packet_update_req;

typedef struct _st_packet_update_resp
{
    sint nResult;                                  // 0：操作成功
}st_packet_update_resp;

//2005    套餐暂停
typedef struct _st_packet_pause_req
{
    char sUserNumber[Len4Mobile + 1];              // 手机号码
    int  nPackId;                                  // 业务所在的套餐
    int  nOPMode;                                  // 操作方式，参见附录“操作方式说明”
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
}st_packet_pause_req;

typedef struct _st_packet_pause_resp
{
    sint nResult;                                  // 0：操作成功
}st_packet_pause_resp;


//2006    套餐恢复
typedef struct _st_packet_resume_req
{
    char sUserNumber[Len4Mobile + 1];              // 手机号码
    int  nPackId;                                  // 业务所在的套餐
    int  nOPMode;                                  // 操作方式，参见附录“操作方式说明”
    int  nSendFlg;
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
}st_packet_resume_req;

typedef struct _st_packet_resume_resp
{
    sint nResult;                                  // 0：操作成功
}st_packet_resume_resp;

//2007    销户
typedef struct _st_user_del_req
{
    char sUserNumber[Len4Mobile + 1];              // 手机号码
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
}st_user_del_req;

typedef struct _st_user_del_resp
{
    sint nResult;                                  // 0：操作成功
}st_user_del_resp;

//2008    换号
typedef struct _st_mobile_update_req
{
    char sUserNumberOld[Len4Mobile + 1];              // 手机号码
    int nMealIdOld;                                   // 套餐号
    int nProvIdOld;                                   // 省份号
    char sUserNumberNew[Len4Mobile + 1];              // 手机号码
    int nMealIdNew;
    int nProvIdNew;
    char sComeFrom[Len4b64 + 1];                   // 操作来源简短说明
}st_mobile_update_req;

typedef struct _st_mobile_update_resp
{
    sint nResult;                                  // 0：操作成功
}st_mobile_update_resp;

//2009    PE激活
typedef struct _st_pe_resume_req
{
    char sUserNumber[Len4Mobile + 1];              // 手机号码
    int  nPackId;                                  // 业务所在的套餐
    st_packet_register_req stReg;
}st_pe_resume_req;

typedef struct _st_pe_resume_resp
{
    sint nResult;                                  // 0：操作成功
}st_pe_resume_resp;

//2011    积分
typedef struct _st_integral_insert_req
{
    char sUserNumber[Len4Mobile + 1];              // 手机号码
    int  nRuleId;                                  // 业务所在的套餐
    char sRecDate[Len4Date + 1];                   // 统计的日期（格式为YYYYMMDD）
    int  nMultiple;                                // 操作的次数
}st_integral_insert_req;

typedef struct _st_integral_insert_resp
{
    sint nResult;                                  // 0：操作成功
    //int  nRuleType;                              // 规则的类型,0:固定积分  1: 常规功能积分2:活动赠送积分 3:兑换积分
    //                                             // 4：竟拍积分 5：冻结积分 6：解冻积分 7：推荐功能积分 8:其他积分
    //                                             
    //int  nRuleInt;                               // 本规则一次产生的积分
    //int  nRuleTop;                               // 本规则封顶类型，0:不封顶  1:按天封顶  2:按次封顶
    //int  nTopInt;                                // 本规则封顶分数
    //int  nProvCode;                              // 用户所在的省份
    //int  nAreaCode;                              // 用户所在的城市
}st_integral_insert_resp;


//2012    邮件到达通知
typedef struct _st_notify_set_req {
    char sUserNumber[Len4Mobile+1];
    char sBeginDate[Len4Date+1];
    char sEndDate[Len4Date+1];
    int  nBeginTime;
    int  nEndTime;
    int  nWeek;
    int  nTypeSelect;
    int  nMNLType;
    int  nLimitFlag;
    int  nLimitType;
    int  nLimitKeyType;
    int  nLimitDomainType;
    int  nLimitIMPType;
    char sLimitKey[Len4LimitKey+1];
    int  nNotifySupply;
    int  nNotifyUseCount;
    char sLimitDomain[Len4LimitDomain+1];
    char sMultitime[Len4LimitKey+1];
    char sMultitimeType[Len4LimitKey+1];
    char sCustomDate[Len4LimitKey+1];
    int  nOrderType;
    char sComeFromDesc[Len4b64+1];

    int  nSendFlg;
    char sMsg[Len4TmpString + 1];                  // 下发短信语
    char sSpsId[Len4SpsId + 1];                    // 下发短信省份ID
    char sSendTime[Len4StartSendTime+1];           // 短信下发时间
}st_notify_set_req;

typedef struct _st_notify_set_resp {
    sint nResult;
}st_notify_set_resp;

//2013    修改账单
typedef struct _st_bill_set_req {
    char sUserNumber[Len4Mobile+1];
    int  nStatus;
    char sBillType[Len4BillType+1];
    int  nBillFlag;
    int  nOrderType;
    char sComeFromDesc[Len4b64+1];
    int  nSendFlg;
    char sMsg[Len4TmpString + 1];                  // 下发短信语
    char sSpsId[Len4SpsId + 1];                    // 下发短信省份ID
    char sSendTime[Len4StartSendTime+1];           // 短信下发时间
}st_bill_set_req;

typedef struct _st_bill_set_resp {
    sint nResult;
}st_bill_set_resp;

//2014    查询配置参数
typedef struct _st_config_query_req {
    char sUserNumber[Len4Mobile+1];
    int  nOpType;
    int  nConfigId;
    //int  nOrderType;
    char sComeFromDesc[Len4b64+1];
}st_config_query_req;

typedef struct _st_config_query_resp {
    sint nResult;
    char sValue[Len4Config+1];
}st_config_query_resp;

//2016    查询套餐的网盘容量
typedef struct _st_netdisk_query_req {
    char sUserNumber[Len4Mobile+1];
    int  nOpType;
    char sComeFromDesc[Len4b64+1];
}st_netdisk_query_req;

typedef struct _st_netdisk_query_resp {
    sint nResult;
    int nCapacity;
}st_netdisk_query_resp;

//5001    139邮箱杀毒订购关系查询
typedef struct _st_kv_query_req
{
    char sUserNumber[Len4Mobile + 1];              // 手机号码
}st_kv_query_req;

typedef struct _st_kv_query_resp
{
    sint nResult; 
    int nOrderType;
	int nSum;
	int nCount;
}st_kv_query_resp;

//5002    使用139邮箱杀毒后次数减一
typedef struct _st_kv_sub_req
{
    char sUserNumber[Len4Mobile + 1];        
    int  nOPMode;                                 
    int  nComeFrom;                                  
    char sComeFrom[Len4b64 + 1];                 
}st_kv_sub_req;

typedef struct _st_kv_sub_resp
{
    sint nResult;                                  // 0：操作成功
}st_kv_sub_resp;


#pragma pack( )  

#endif

