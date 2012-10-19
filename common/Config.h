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


//����ÿ��������ļ���ֵ��
typedef list <st_FieldData> ConfigFieldlist;
typedef map  <string, ConfigFieldlist > ConfigDominMap;

//��ȡ�����ļ���
class Config
{
public:
    Config(void);
    ~Config(void);

    static Config * instance();

    //���������ļ�����·��������������ڴ�
    int Load(const char *pPath);
    
    //���¼���������
    int Reload(void);

	//��ȡĳ������������Ŀ��ֵ,�ɹ�����0��ʧ�ܷ���-1
	int ReadField(const char *strDomainName,const char * strFieldName, string &strValue);

	
	//��ȡĳ������������Ŀ��ֵ,�ɹ�����0��ʧ�ܷ���-1
	int ReadField(const char *strDomainName,const char * strFieldName,int nlen,char sValue[]);

    //��ȡĳ�����������������
    int ReadDomin(const char *strDomainName,ConfigFieldlist &listVal);

    //��ȡĳ��������ָ��fieldName����������
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
