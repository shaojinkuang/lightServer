#ifndef _CONFIG_BASE_H_
#define _CONFIG_BASE_H_

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
class config
{
public:
	static config * instance();
    config(void);
    ~config(void);

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
    config(const config &);
    config &operator=(const config &);

private:
    char *m_pPath;
    pthread_mutex_t  m_lstMutex;
    ConfigDominMap m_ConfigMap;
};

#endif
