#include "config.h"
#include <string.h>
#include <strings.h>

#define MAX_BUF 1024

config * config:: instance()
{
    static config inst;
    return &inst;
}

config::config(void)
{
    m_pPath = NULL;

    pthread_mutex_init(&this->m_lstMutex, NULL);
}

config::~config(void)
{
    if (m_pPath)
    {
        delete []m_pPath;
    }

    pthread_mutex_destroy(&this->m_lstMutex);
}

int config::Load(const char *pPath)
{
    char szBuf[MAX_BUF] = {0};
    FILE *fp;
    const char *p1 = NULL;
    const char *p2 = NULL;
    string strDomin; 
    string strKey;
    string strValue;
    string strTemp;
    int    nPos = -1;

    if (NULL == pPath)
    {
        return -1;
    }

    //如果文件打开失败，返回
    fp = fopen(pPath, "r+");
    if (NULL == fp)
    {
        return -1;
    }

    pthread_mutex_lock(&this->m_lstMutex);

    m_ConfigMap.clear();

    //读取配置文件项
    while (fgets(szBuf, MAX_BUF - 1, fp) != NULL)
    {
        if ('#' == *szBuf || '\0' == *szBuf)
        {
            continue;
        }

        p1 = strchr(szBuf, '[');
        if (p1 != NULL)
        {
            p2 = strrchr(szBuf, ']');
            if (p2 != NULL)
            {
                strDomin.clear();
                copy(p1 + 1, p2, back_inserter(strDomin));
            }
            else
            {
                //错误的编辑项，关闭文件并返回错误
                fclose(fp);
                pthread_mutex_unlock(&this->m_lstMutex);
                return -1;
            }
        }
        else
        {
            p1 = strchr(szBuf, '=');
            if (p1 != NULL)
            {
                strKey.clear();
                strKey.append(szBuf,0,p1-szBuf);
			
                //判断strKey里面是否包含注释符
                if (strchr(strKey.c_str(), '#') != NULL)
                {
                    continue;
                }

                //去掉strKey前后的空格和tab键
                strKey.erase(0, strKey.find_first_not_of(" \t\n"));
                strKey.erase(strKey.find_last_not_of(" \t") + 1);
     
                if ((nPos = strKey.find_last_of("\r")) != string::npos)
                {
                    strKey.erase(nPos);
                }
                
                p2 = strrchr(szBuf, '\n');
                if (NULL == p2)
                {
                    p2 = szBuf + strlen(szBuf);
                }

                strTemp.clear();
                strTemp.append(szBuf,p1-szBuf+1, p2-p1-1);
				int nIndex = 0;

                //判断值里面是否包含注释符
                p1 = strchr(strTemp.c_str(), '#');
                if (p1 != NULL)
                {
                    strValue.clear();
                    strValue.append(strTemp,0,p1-strTemp.c_str()-1);
                }
                else
                {
                    strValue = strTemp;
                }

                //去掉值前后的空格和tab键
                strValue.erase(0, strValue.find_first_not_of(" \t\n"));
                strValue.erase(strValue.find_last_not_of(" \t") + 1);
                if ((nPos = strValue.find_last_of("\r")) != string::npos)
                {
                    strValue.erase(nPos);
                }

				ConfigDominMap::iterator iter;
				if ((iter = m_ConfigMap.find(strDomin)) != m_ConfigMap.end())
				{
				    st_FieldData stData;
                    stData.strFieldName = strKey;
                    stData.strVal = strValue;
				    iter->second.push_back(stData);
				}
				else
				{
				    ConfigFieldlist Fieldlist;
                    st_FieldData stData;
                    stData.strFieldName = strKey;
                    stData.strVal = strValue;
				    Fieldlist.push_back(stData);
                    m_ConfigMap.insert(pair<string,ConfigFieldlist>(strDomin, Fieldlist));
				}
            }
        }
    }

    pthread_mutex_unlock(&this->m_lstMutex);

    fclose(fp);

    //如果有路径信息，先判断是否是Reload调用该接口
    if (m_pPath)
    {
        if (strcmp(pPath, m_pPath) == 0)
        {
            return 0;
        }

        delete []m_pPath;
    }

    m_pPath = new char[strlen(pPath) + 1];
    strcpy(m_pPath, pPath);

    return 0;
}

int config::Reload(void)
{
    return this->Load(m_pPath);
}

//获取某个配置项子项目的值
int config::ReadField(const char *strDomainName,const char * strFieldName, string &strValue)
{
	if (strDomainName == NULL || strFieldName == NULL)
	{
		return -1; 
	}

	ConfigDominMap::iterator iter;
    pthread_mutex_lock(&this->m_lstMutex);
	iter = m_ConfigMap.find(strDomainName);
	if (iter == m_ConfigMap.end())
	{
		pthread_mutex_unlock(&this->m_lstMutex);
		return -1;
	}

    bool bFind = false;

	ConfigFieldlist::iterator iterField = iter->second.begin();
	for (; iterField != iter->second.end(); ++iterField)
	{
	    if (strcmp((*iterField).strFieldName.c_str(),strFieldName) == 0)
        {
            bFind = true;
            strValue = (*iterField).strVal;
            break;
        }
		
	}

    if (!bFind)
    {
       pthread_mutex_unlock(&this->m_lstMutex);
	   return -1;
    }
    
    pthread_mutex_unlock(&this->m_lstMutex);
    
    return 0;
}

int config::ReadField(const char *strDomainName,const char * strFieldName,int nlen,char sValue[])
{
    if (strDomainName == NULL || strFieldName == NULL)
	{
		return -1; 
	}

	ConfigDominMap::iterator iter;
    pthread_mutex_lock(&this->m_lstMutex);
	iter = m_ConfigMap.find(strDomainName);
	if (iter == m_ConfigMap.end())
	{
		pthread_mutex_unlock(&this->m_lstMutex);
		return -1;
	}

    bool bFind = false;
	ConfigFieldlist::iterator iterField = iter->second.begin();
	for (; iterField != iter->second.end(); ++iterField)
	{
	    if (strcmp((*iterField).strFieldName.c_str(),strFieldName) == 0)
        {
            bFind = true;       
            nlen = nlen > (*iterField).strVal.size() ? (*iterField).strVal.size() : nlen;
            strncpy(sValue,(*iterField).strVal.c_str(),nlen);
            sValue[nlen] = 0x0;
            break;
        }
		
	}

    if (!bFind)
    {
       pthread_mutex_unlock(&this->m_lstMutex);
	   return -1;
    }
    
    pthread_mutex_unlock(&this->m_lstMutex);
    
    return 0; 
}
//获取某个配置项内容（一般是主配置项内容）
int config::ReadDomin(const char *strDomainName,ConfigFieldlist &listVal)
{
	listVal.clear();

	if (strDomainName == NULL )
	{
		return -1; 
	}

	ConfigDominMap::iterator iter;
	pthread_mutex_lock(&this->m_lstMutex);
	iter = m_ConfigMap.find(strDomainName);
	if (iter == m_ConfigMap.end())
	{
		pthread_mutex_unlock(&this->m_lstMutex);
		return -1;
	}

	listVal = iter->second;
	pthread_mutex_unlock(&this->m_lstMutex);
	return 0;
}


int config::ReadDomin(const char *strDomainName,const char * strFieldName,ConfigFieldlist &listVal)
{
    if (strDomainName == NULL || strFieldName == NULL)
	{
		return -1; 
	}

    listVal.clear();

	ConfigDominMap::iterator iter;
    pthread_mutex_lock(&this->m_lstMutex);
	iter = m_ConfigMap.find(strDomainName);
	if (iter == m_ConfigMap.end())
	{
		pthread_mutex_unlock(&this->m_lstMutex);
		return -1;
	}

    if (strlen(strFieldName) == 0)
    {
        listVal = iter->second;
        pthread_mutex_unlock(&this->m_lstMutex);
        return 0;
    }
    
	ConfigFieldlist::iterator iterField = iter->second.begin();
	for (; iterField != iter->second.end(); ++iterField)
	{
	    if (strcmp((*iterField).strFieldName.c_str(),strFieldName) == 0)
        {
           listVal.push_back(*iterField);
        }
		
	}

    pthread_mutex_unlock(&this->m_lstMutex);
    
    return 0;
}
