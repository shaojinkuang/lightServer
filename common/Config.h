#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdio.h>
#include <algorithm>
#include <iterator>
#include <map>
#include <list>
#include <string>
#include <vector>
#include <pthread.h>


using namespace std;

typedef struct _st_FieldData
{
   string strFieldName;
   string strVal;
   
}st_FieldData;


//保存每个配置项的键、值对
typedef list <st_FieldData> ConfigFieldlist;
typedef map  <string, ConfigFieldlist > ConfigDominMap;

//读取配置文件类
class Config
{
public:
    Config(void);
    ~Config(void);

    static Config * instance();

    //传入配置文件名称路径，加载配置项到内存
    int Load(const char *pPath);
    
    //重新加载配置项
    int Reload(void);

	//获取某个配置项子项目的值,成功返回0，失败返回-1
	int ReadField(const char *strDomainName,const char * strFieldName, string &strValue);

	
	//获取某个配置项子项目的值,成功返回0，失败返回-1
	int ReadField(const char *strDomainName,const char * strFieldName,int nlen,char sValue[]);

    //获取某个配置项得所有内容
    int ReadDomin(const char *strDomainName,ConfigFieldlist &listVal);

    //获取某个配置项指定fieldName的所有内容
	int ReadDomin(const char *strDomainName,const char * strFieldName,ConfigFieldlist &listVal);

private:
    Config(const Config &);
    Config &operator=(const Config &);

private:
    char *m_pPath;
    pthread_mutex_t  m_lstMutex;
    ConfigDominMap m_ConfigMap;
};

#endif
