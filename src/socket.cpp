// -------- Start of File -------- //
// C++ Source Code File Name: Socket.cpp
// C++ Compiler Used: SOLARIS CC,GCC 
// Produced By: RBI Software Development Team
// File Creation Date: 06/21/2004
// Date Last Modified: 06/24/2004
// Copyright (c) 2001-2004 RBI Software Development
// ----------------------------------------------------------- // 
// ------------- Program Description and Details ------------- // 
// ----------------------------------------------------------- // 
//The CSocket class is an object-oriented wrapper used to create 
//TCP/IP sockets on UNIX and LINUX platforms. The CSocket class 
//supports stream sockets and datagram sockets and includes 
//several low-level functions needed by derived classes to establish 
//communication end-points and transfer data.
//

// Changes:
// ==============================================================
// 06/21/2004: 

#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include "socket.h"
#include <stdio.h>

#if defined(__UNIX__) && !defined(__BSD_UNIX__)  
#include <sys/utsname.h> // Used for host name information
#endif // __UNIX__

//定义socket错误提示信息
const int MaxSocketExceptionMessages = 28;
const char *SocketExceptionMessages[MaxSocketExceptionMessages] = 
{
	"Socket exception: No exception reported",             // NO_ERROR
	"Socket exception: Invalid exception code",            // INVALID_ERROR_CODE 
	"Socket exception: Error accepting remote socket",     // ACCEPT_ERROR
	"Socket exception: Could not bind socket",             // BIND_ERROR
	"Socket exception: Buffer overflow error",             // BUFOVER_ERROR
	"Socket exception: Could not connect socket",          // CONNECT_ERROR
	"Socket exception: Socket has been disconnected",      // DISCONNECTED_ERROR
	"Socket exception: File length does not match expected value", // FILELENGTH
	"Socket exception: File modification time dates do not match", // FILEMODTIME
	"Socket exception: A file system error occurred",      // FILESYSTEM_ERROR
	"Socket exception: Error getting socket option",       // GETOPTION_ERROR
	"Socket exception: Could not resolve hostname",        // HOSTNAME_ERROR
	"Socket exception: Initialization error",              // INIT_ERROR
	"Socket exception: Listen error",                      // LISTEN_ERROR
	"Socket exception: Get peer name error",               // PEER_ERROR
	"Socket exception: Unsupported protocol requested",    // PROTOCOL_ERROR
	"Socket exception: Receive error",                     // RECEIVE_ERROR
	"Socket exception: Request timed out",                 // REQUEST_TIMEOUT
	"Socket exception: Unsupported service requested",     // SERVICE_ERROR
	"Socket exception: Error setting socket option",       // SETOPTION_ERROR
	"Socket exception: Get socket name error",             // SOCKNAME_ERROR
	"Socket exception: Unsupported socket type requested", // SOCKETTYPE
	"Socket exception: Transmit error",                    // TRANSMIT_ERROR
		
	"Socket exception: Database block acknowledgment error",  // BLOCKACK
	"Socket exception: Bad database block header",            // BLOCKHEADER
	"Socket exception: Bad database block size",              // BLOCKSIZE
	"Socket exception: Database block synchronization error", // BLOCKSYNC
	
	"FTP protocol reported an error condition",      // FTP_ERROR
};

//////////////////////////////////////////////////////////////////////
//  Function:       	CSocket();
//  Description:    	CSocket的构造函数，负责初始化CSocket的成员变量
//  Calls:           	Clear	//初始化成员变量
//  Data Accessed: 		无
//  Data Updated:   	无
//  Input:          	无
//  Output:         	无
//  Return:         	无
//////////////////////////////////////////////////////////////////////
CSocket::CSocket()
{
	Clear();

#if defined (__WIN32__)
	// Initialize the WinSock DLL with the specified version.
	WORD wVersionRequested = MAKEWORD(1, 1);
	WSADATA socket_data;
	int rv = WSAStartup(wVersionRequested, &socket_data);
#endif
  
}

//////////////////////////////////////////////////////////////////////
//  Function:       	void Clear();
//  Description:    	初始化成员变量。
//  Calls:           	无
//  Data Accessed: 		无
//  Data Updated:   	address_family、socket_type、protocol_family、port_number、local_ socket、remote_socket、bytes_read、bytes_moved、is_connected、is_bound、socket_error
//  Input:          	无
//  Output:         	无
//  Return:         	无
//////////////////////////////////////////////////////////////////////
void CSocket::Clear()
{
	address_family	= AF_INET;				// Default address family
	socket_type		= SOCK_STREAM;			// Default socket type
	protocol_family = IPPROTO_TCP;			// Default protocol family
	port_number		= SOCKET_DEFAULT_PORT;	// Default port number
	local_socket	= -1;
	remote_socket	= -1;
	bytes_read		= 0;
	bytes_write		= 0;
	is_connected	= 0;
	is_bound		= 0;
	socket_error	= SOCKET_NO_ERROR;
	
	// Zero-out the Internet address structures
	memset(&local_sin, 0, sizeof(sockaddr_in));
	memset(&remote_sin, 0, sizeof(sockaddr_in));
}


//////////////////////////////////////////////////////////////////////
//  Function:       	~CSocket();
//  Description:    	CSocket的析构函数，负责CSocket的资源释放
//  Calls:           	Close	//关闭所有socket连接
//  Data Accessed: 		无
//  Data Updated:   	无
//  Input:          	无
//  Output:         	无
//  Return:         	无
//////////////////////////////////////////////////////////////////////
CSocket::~CSocket()
{
	Close();
}

//////////////////////////////////////////////////////////////////////
//  Function:       	void Close();
//  Description:    	关闭所有Socket。
//  Calls:           	无
//  Data Accessed: 		无
//  Data Updated:   	bytes_moved、bytes_read、is_connected、is_bound、local_socket、remote_socket
//  Input:          	无
//  Output:         	无
//  Return:         	无
//////////////////////////////////////////////////////////////////////
void CSocket::Close()
{
	bytes_write	 = 0;
	bytes_read	 = 0;
	is_connected = 0;
	is_bound	 = 0;
	if (local_socket != -1)
	{
#if defined (__WIN32__)
		closesocket(local_socket);
#else
		close(local_socket);
#endif
		local_socket = -1;
	}
	if (remote_socket != -1)
	{
#if defined (__WIN32__)
		closesocket(remote_socket);
#else
		close(remote_socket);
#endif
		remote_socket = -1;
	}
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int InitSocket(int nSocketType, int nPort, char* szHost);
//  Description:    	创建并初始化Socket，
//  Calls:           	无
//  Data Accessed: 		无
//  Data Updated:   	socket_type
//						protocol_family
//						address_family
//						port_number
//						local_sin
//						socket_error
//  Input:          	nSocketType	Socket类型，有SOCK_STREAM和SOCK_DGRAM两种
//						nPort	  	端口号
//						szHost		主机名字，如果该字符串不为空，则初始化Socket为服务端Socket
//  Output:         	无
//  Return:         	初始化成功的Socket ID，如返回-1则初始化失败
//////////////////////////////////////////////////////////////////////
int CSocket::InitSocket(int nSocketType, int nPort, const char* szHost)
{
	//设置socket_type、protocol_family、address_family、port_number

    char pb[8]={0};
    snprintf(pb, 7, "%d", nPort);

    struct addrinfo hints, *res, *res0;
    int error;

    memset(&hints, 0, sizeof(hints)); 
    hints.ai_family = AF_INET; 
    hints.ai_socktype = nSocketType; 

	if(hints.ai_socktype == SOCK_STREAM) 
	{
        hints.ai_protocol = IPPROTO_TCP;
	}
	else if(hints.ai_socktype == SOCK_DGRAM) 
	{
        hints.ai_protocol = IPPROTO_UDP;
	}
	else 
	{
		socket_error = SOCKET_SOCKETTYPE_ERROR;
		return -1;
	}

    address_family = hints.ai_family;
    socket_type = hints.ai_socktype;
    protocol_family = hints.ai_protocol;
    port_number = nPort;

	//Put the server information into the server structure.
	//local_sin.sin_family = address_family;
	
	//设置local_sin
	if (szHost == NULL) {
		//local_sin.sin_addr.s_addr = INADDR_ANY;
        hints.ai_flags = AI_PASSIVE;
	}

    error = getaddrinfo(szHost, pb, &hints, &res0);

    /*NOTREACHED*/ 
    if (error) {            
		socket_error = SOCKET_HOSTNAME_ERROR;
        return -1; 
    }   

    //取得主机地址
    int sock;
    for (res = res0; res; res = res->ai_next) {
        memcpy(&local_sin, res->ai_addr, res->ai_addrlen);
    }
    freeaddrinfo(res0);

	//local_sin.sin_port = htons(port_number); 
	
	//创建socket
	local_socket = socket(address_family, socket_type, protocol_family);
	//检查socket创建是否成功
#if defined (__WIN32__)
	// The SOCKET type is unsigned in the WinSock library
	if(local_socket == INVALID_SOCKET) // Defined as (SOCKET)(~0)
#else
	if(local_socket < 0)
#endif
	{
		socket_error = SOCKET_INIT_ERROR;
		return -1;
	}

	return local_socket;

//    //设置socket_type、protocol_family、address_family、port_number
//    if(nSocketType == SOCK_STREAM) 
//    {
//        socket_type = SOCK_STREAM;
//        protocol_family = IPPROTO_TCP;
//    }
//    else if(nSocketType == SOCK_DGRAM) 
//    {
//        socket_type = SOCK_DGRAM;
//        protocol_family = IPPROTO_UDP;
//    }
//    else 
//    {
//        socket_error = SOCKET_SOCKETTYPE_ERROR;
//        return -1;
//    }
//    address_family = AF_INET;
//    port_number = nPort;
//
//    //Put the server information into the server structure.
//    local_sin.sin_family = address_family;
//    
//    //设置local_sin
//    if (szHost == NULL)
//    {
//        local_sin.sin_addr.s_addr = INADDR_ANY;
//    }
//    else
//    {
//        //取得主机地址
//        hostent *hostnm = gethostbyname(szHost); 
//        if(hostnm == NULL) 
//        {
//          socket_error = SOCKET_HOSTNAME_ERROR;
//          return -1;
//        }
//        local_sin.sin_addr.s_addr = *((unsigned long *)hostnm->h_addr);
//    }
//    // The port must be put into network byte order.
//    // htons()--"Host to Network Short" 
//    // htonl()--"Host to Network Long" 
//    // ntohs()--"Network to Host Short" 
//    // ntohl()--"Network to Host Long" 
//    local_sin.sin_port = htons(port_number); 
//    
//    //创建socket
//    local_socket = socket(address_family, socket_type, protocol_family);
//    //检查socket创建是否成功
//#if defined (__WIN32__)
//    // The SOCKET type is unsigned in the WinSock library
//    if(local_socket == INVALID_SOCKET) // Defined as (SOCKET)(~0)
//#else
//    if(local_socket < 0)
//#endif
//    {
//        socket_error = SOCKET_INIT_ERROR;
//        return -1;
//    }
//
//    return local_socket;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int Connect();
//  Description:    	连接Socket，连接的Socket为InitSocket函数中初始化的Socket。
//  Calls:           	无
//  Data Accessed: 		local_socket、local_sin
//  Data Updated:   	is_connected、socket_error
//  Input:          	无
//  Output:         	无
//  Return:         	返回0则成功，返回其它则为错误码
//  Others:         	伪码如下
//////////////////////////////////////////////////////////////////////
int CSocket::Connect()
{
    SetTime( 5 );
	
	int rv = connect(local_socket, (struct sockaddr*)&local_sin, sizeof(local_sin));
	if(rv >= 0) 
	{
		is_connected = 1; 
	}
	else 
	{
		socket_error = SOCKET_CONNECT_ERROR;
		is_connected = 0;
	}

	SetTime( 15 );
	
	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int Accept();
//  Description:    	接受一个远端Socket的连接，该函数直到接受一个远端Socket连接才返回。
//  Calls:           	无
//  Data Accessed: 		remote_sin
//  Data Updated:   	socket_error、remote_socket
//  Input:          	无
//  Output:         	无
//  Return:         	连接成功的远端Socket ID，如返回-1则失败
//////////////////////////////////////////////////////////////////////
int CSocket::Accept()
{
	int addr_size = sizeof(remote_sin);
#if defined (__LINUX__)
	remote_socket = accept(local_socket, (struct sockaddr *)&remote_sin, (socklen_t*)&addr_size);
#else
    remote_socket = accept(local_socket, (struct sockaddr *)&remote_sin, &addr_size);
#endif
	//判断remote_socket值，如失败则设定socket_error
	if(remote_socket < 0)
    {
		socket_error = SOCKET_ACCEPT_ERROR;
		return -1;
    }
	
	return remote_socket;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int Bind();
//  Description:    	绑定Socket以便接收数据。
//  Calls:           	无
//  Data Accessed: 		local_socket、local_sin
//  Data Updated:   	is_bound、socket_error
//  Input:          	无
//  Output:         	无
//  Return:         	返回0则成功，返回其它则为错误码
//////////////////////////////////////////////////////////////////////
int CSocket::Bind()
{
	int rv = bind(local_socket, (struct sockaddr *)&local_sin, sizeof(local_sin));
	//判断rv值，设置is_bound，如失败则设置socket_error
	if(rv >= 0) 
	{
		is_bound = 1;
	} 
	else 
	{
		socket_error = SOCKET_BIND_ERROR;
		is_bound = 0;
	}
	
	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int Listen(int nMaxConnections);
//  Description:    	如果配置成服务端Socket的话监听连接，允许的最大连接数目为传入的最大连接数。
//  Calls:           	无
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error
//  Input:          	nMaxConnections	最大连接数
//  Output:         	无
//  Return:         	返回0则成功，返回其它则为错误码
//////////////////////////////////////////////////////////////////////
int CSocket::Listen(int nMaxConnections)
{
	int rv = listen(local_socket, nMaxConnections); 
	//判断rv值，如失败则设置socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_LISTEN_ERROR;
	}

	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int SetSockOpt(int nLevel, int nOptName, const void *pOptVal,  int nOptLen);
//  Description:    	设置Socket的参数。
//  Calls:           	无
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error
//  Input:          	nLevel		协议标准
//           			nOptName	参数编号
//           			pOptVal		参数指针
//						uOptLen		参数长度
//  Output:         	无
//  Return:         	返回0则成功，返回其它则为错误码
//////////////////////////////////////////////////////////////////////
int CSocket::SetSockOpt(int nLevel, int nOptName, const void *pOptVal,  int nOptLen)
{
#if defined (__WIN32__)
	int rv = setsockopt(local_socket, nLevel, nOptName, (const char*)pOptVal, nOptLen);
#else
	int rv = setsockopt(local_socket, nLevel, nOptName, pOptVal, nOptLen);
#endif	
	//判断rv值，如失败则设置socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_SETOPTION_ERROR;
	}

	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int GetSockOpt(int nLevel, int nOptName, const void *pOptVal,  int* pOptLen);
//  Description:    	获取Socket的参数。
//  Calls:           	无
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error
//  Input:          	nLevel		协议标准
//           			nOptName	参数编号
//  Output:         	pOptVal		参数指针
//						pOptLen		参数长度
//  Return:         	返回0则成功，返回其它则为错误码
//////////////////////////////////////////////////////////////////////
int CSocket::GetSockOpt(int nLevel, int nOptName, void *pOptVal,  int* pOptLen)
{
#if defined (__WIN32__)
	int rv = getsockopt(local_socket, nLevel, nOptName, (char*)pOptVal, pOptLen);
#elif defined (__LINUX__)
    int rv = getsockopt(local_socket, nLevel, nOptName, (char*)pOptVal, (socklen_t*)pOptLen);
#else
	int rv = getsockopt(local_socket, nLevel, nOptName, pOptVal, pOptLen);
#endif	
	//判断rv值，如失败则设置socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_GETOPTION_ERROR;
	}

	return rv;
}


//////////////////////////////////////////////////////////////////////
//  Function:       	int GetSockName(int nSocket, sockaddr_in* sa);
//  Description:    	获取Socket的参数。
//  Calls:           	无
//  Data Accessed: 	无
//  Data Updated:   	无
//  Input:          	nSocket	Socket ID
//  Output:         	sa		地址信息
//  Return:         	返回0则成功，返回其它则为错误码
//////////////////////////////////////////////////////////////////////
int CSocket::GetSockName(int nSocket, sockaddr_in* sa)
{
	int namelen = sizeof(sockaddr_in);
#if defined (__LINUX__)
	int rv = getsockname(nSocket, (struct sockaddr *)sa, (socklen_t*)&namelen);
#else
	int rv = getsockname(nSocket, (struct sockaddr *)sa, &namelen);
#endif
	//判断rv值，如失败则设置socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_SOCKNAME_ERROR;
	}

	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int GetHostName(char *szBuf);
//  Description:    	获取主机名字。
//  Calls:           	无
//  Data Accessed: 		无
//  Data Updated:   	socket_error
//  Input:          	无
//  Output:         	szBuf		主机名字
//  Return:         	返回0则成功，返回其它则为错误码
//////////////////////////////////////////////////////////////////////
int CSocket::GetHostName(char *szBuf)
{
	if (szBuf == NULL) 
	{
		return -1;
	}
	//调用gethostname获取主机名字
	int rv = gethostname(szBuf, MAX_NAME_LEN);
	//判断返回值，如失败则设置socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_HOSTNAME_ERROR;
	}
	
	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	hostent GetHostInformation(char *szHostName);
//  Description:    	获取主机信息。
//  Calls:           	无
//  Data Accessed: 		无
//  Data Updated:   	socket_error
//  Input:          	szHostName	主机名字
//  Output:         	无
//  Return:         	主机信息
//////////////////////////////////////////////////////////////////////
hostent* CSocket::GetHostInformation(char *szHostName)
{
	in_addr hostia;
	hostent *hostinfo;
	hostia.s_addr = inet_addr(szHostName);

	#ifndef INADDR_NONE // IPv4 Internet address integer constant not defined
	#define INADDR_NONE 0xffffffff
	#endif

	if(hostia.s_addr == INADDR_NONE) 
	{ 
		// Look up host by name
		hostinfo = gethostbyname(szHostName); 
	}
	else 
	{  
		// Look up host by IP address
		hostinfo = gethostbyaddr((const char *)&hostia, sizeof(in_addr), AF_INET);
	}
	if(hostinfo == NULL) 
	{ 
		// No host name info avialable
		return hostinfo;
	}
	
	hostent *buf = new hostent;
	if(buf ==  NULL) 
	{
		// Memory allocation error
		return 0; 
	}
	memmove(buf, hostinfo, sizeof(hostent));

	return buf;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	SocketError SetSocketError(SocketError err);
//  Description:    	设置Socket错误代码。
//  Calls:           	无
//  Data Accessed: 		无
//  Data Updated:   	socket_error
//  Input:          	err	错误代码
//  Output:         	无
//  Return:         	错误代码
//////////////////////////////////////////////////////////////////////
SocketError CSocket::SetSocketError(SocketError err)
{
	return socket_error = err;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	SocketError GetSocketError();
//  Description:    	获取Socket错误代码。
//  Calls:           	无
//  Data Accessed: 		socket_error 
//  Data Updated:   	无
//  Input:          	szHostName	主机名字
//  Output:         	无
//  Return:         	错误代码
//////////////////////////////////////////////////////////////////////
SocketError CSocket::GetSocketError()
{
	return socket_error;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	const char *gxSocket::SocketExceptionMessage();
//  Description:    	获取Socket错误代码的提示信息。
//  Calls:           	无
//  Data Accessed: 		socket_error 
//  Data Updated:   	无
//  Input:          	无
//  Output:         	无
//  Return:         	错误代码的提示信息
//////////////////////////////////////////////////////////////////////
const char* CSocket::SocketExceptionMessage()
{
	if((int)socket_error > (MaxSocketExceptionMessages-1))
	{
		socket_error = SOCKET_INVALID_ERROR_CODE;
	}
	
	int error = (int)socket_error;
	return SocketExceptionMessages[error];
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int RawRead(void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	从local_socket读取数据，可能在所有的数据被接收前返回。
//  Calls:           	无
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error、bytes_read
//  Input:          	pBuf        缓冲区指针
//          			nBytes		缓冲区大小
//          			nFlags		读取标记
//  Output:         	pBuf		读取的数据
//  Return:         	返回读取的字节数，如返回-1则遇到错误
//////////////////////////////////////////////////////////////////////
int CSocket::RawRead(void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_read = 0;
	bytes_read = recv(local_socket, (char *)pBuf, nBytes, nFlags);
	//判断bytes_read，如错误则设置socket_error
	if(bytes_read < 0) 
	{	
		socket_error = SOCKET_RECEIVE_ERROR;
	}
	if(bytes_read == 0) 
	{	
		socket_error = SOCKET_DISCONNECTED_ERROR;
	}
	
	return bytes_read;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int RawWrite(const void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	向local_socket写入数据，可能在所有的数据被写入前返回。
//  Calls:           	无
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error、bytes_write
//  Input:          	pBuf        写入的数据
//          			nBytes		写入数据大小
//          			nFlags		写入标记
//  Output:         	无
//  Return:         	返回写入的字节数，如返回-1则遇到错误
//////////////////////////////////////////////////////////////////////
int CSocket::RawWrite(const void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_write = 0;
	bytes_write = send(local_socket, (char *)pBuf, nBytes, nFlags);
	//判断bytes_write，如错误则设置socket_error
	if(bytes_write < 0) 
	{
		socket_error = SOCKET_TRANSMIT_ERROR;
	}

	return bytes_write;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int RawRemoteRead(void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	从remote_socket读取数据，可能在所有的数据被接收前返回。
//  Calls:           	无
//  Data Accessed: 		remote_socket
//  Data Updated:   	socket_error、bytes_read
//  Input:          	pBuf        缓冲区指针
//          			nBytes		缓冲区大小
//          			nFlags		读取标记
//  Output:         	pBuf		读取的数据
//  Return:         	返回读取的字节数，如返回-1则遇到错误
//////////////////////////////////////////////////////////////////////
int CSocket::RawRemoteRead(void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_read = 0;
	bytes_read = recv(remote_socket, (char *)pBuf, nBytes, nFlags);
	//判断bytes_read，如错误则设置socket_error
	if(bytes_read < 0) 
	{	
		socket_error = SOCKET_RECEIVE_ERROR;
	}
	if(bytes_read == 0) 
	{	
		socket_error = SOCKET_DISCONNECTED_ERROR;
	}
	
	return bytes_read;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int RawRemoteWrite(const void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	向remote_socket写入数据，可能在所有的数据被写入前返回。
//  Calls:           	无
//  Data Accessed: 		remote_socket
//  Data Updated:   	socket_error、bytes_write
//  Input:          	pBuf        写入的数据
//          			nBytes		写入数据大小
//          			nFlags		写入标记
//  Output:         	无
//  Return:         	返回写入的字节数，如返回-1则遇到错误
//////////////////////////////////////////////////////////////////////
int CSocket::RawRemoteWrite(const void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_write = 0;
	bytes_write = send(remote_socket, (char *)pBuf, nBytes, nFlags);
	//判断bytes_write，如错误则设置socket_error
	if(bytes_write < 0) 
	{
		socket_error = SOCKET_TRANSMIT_ERROR;
	}

	return bytes_write;
}


//////////////////////////////////////////////////////////////////////
//  Function:       	int WaitMsg( int nSeconds );
//  Description:    	接收前超时判断
//  Calls:           	无
//  Data Accessed: 		local_socket
//  Input:          	nSeconds        超时时间
//  Output:         	
//  Return:         	返回0表示成功，-1表示超时
//////////////////////////////////////////////////////////////////////

int CSocket::WaitMsg( int nSeconds )
{
    //定义超时时间
    struct timeval timeout;
    timeout.tv_sec  = nSeconds;
    timeout.tv_usec = 0;
    //初始化等待socket事件参数
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET( local_socket, &fds);

    //等待响应
    if( select(local_socket+1, &fds, 0, 0, &timeout) == 0 ) 
    { 
    	//设置socket错误码
    	socket_error = SOCKET_REQUEST_TIMEOUT;      
    	return -1;
    }

    return 0;
}


//////////////////////////////////////////////////////////////////////
//  Function:       	int Recv(void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	从local_socket读取数据，在所有的数据都被接收后返回。
//  Calls:           	无
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error、bytes_read
//  Input:          	pBuf        缓冲区指针
//          			nBytes		缓冲区大小
//          			nFlags		读取标记
//  Output:         	pBuf		读取的数据
//  Return:         	返回读取的字节数，如返回-1则遇到错误
//////////////////////////////////////////////////////////////////////
int CSocket::Recv(void *pBuf, int nBytes, int nSeconds, int nFlags )
{
	assert(pBuf != NULL);
	
	bytes_read = 0;			// Reset the byte counter
	int num_read = 0;		// Actual number of bytes read
	int num_req = nBytes;		// Number of bytes requested 
	char *pRecvBuf = (char *)pBuf;	// Pointer to the buffer

	//定义超时时间
	struct timeval timeout;
	timeout.tv_sec  = nSeconds;
	timeout.tv_usec = 0;
	//初始化等待socket事件参数
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET( local_socket, &fds);
	
	while( bytes_read < nBytes ) 
	{ 
		//等待响应
		if( select(local_socket+1, &fds, 0, 0, &timeout) == 0 ) 
		{ 
			//设置socket错误码
			socket_error = SOCKET_REQUEST_TIMEOUT;      
		    return -1;
		}

		// Loop until the buffer is full
		num_read = recv( local_socket, pRecvBuf, num_req-bytes_read, nFlags );

		if( num_read  > 0 ) 
		{
			bytes_read += num_read;   // Increment the byte counter
			pRecvBuf += num_read;     // Move the buffer pointer for the next read
		}
		else if ( num_read == 0 && bytes_read > 0  )
		{
			break;
		}
		else if( num_read == 0) 
		{ 
			// Gracefully closed
			socket_error = SOCKET_DISCONNECTED_ERROR;
			return -1; // An error occurred during the read
		}
		else 
		{
			socket_error = SOCKET_RECEIVE_ERROR;
			return -1; // An error occurred during the read
		}
	}
	
	return bytes_read;
}


//////////////////////////////////////////////////////////////////////
//  Function:       	int Send(const void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	向local_socket写入数据，在所有的数据都被写入后返回。
//  Calls:           	无
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error、bytes_write
//  Input:          	pBuf        写入的数据
//          			nBytes		写入数据大小
//          			nFlags		写入标记
//  Output:         	无
//  Return:         	返回写入的字节数，如返回-1则遇到错误
//////////////////////////////////////////////////////////////////////
int CSocket::Send(const void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_write = 0;				// Reset the byte counter
	int num_moved = 0;				// Actual number of bytes written
	int num_req = nBytes;			// Number of bytes requested 
	char* pSendBuf = (char*)pBuf;	// Pointer to the buffer
	
	while(bytes_write < nBytes) 
	{ 
		// Loop until the buffer is empty
		num_moved = send(local_socket, pSendBuf, num_req-bytes_write, nFlags);
		if(num_moved > 0)
		{
			bytes_write += num_moved;  // Increment the byte counter
			pSendBuf += num_moved;     // Move the buffer pointer for the next read
		}
		else 
		{
			socket_error = SOCKET_TRANSMIT_ERROR;
			return -1; // An error occurred during the read
		}
	}
	
	return bytes_write;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int RemoteRecv(void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	从remote_socket读取数据，在所有的数据都被接收后返回。
//  Calls:           	无
//  Data Accessed: 		remote_socket
//  Data Updated:   	socket_error、bytes_read
//  Input:          	pBuf        缓冲区指针
//          			nBytes		缓冲区大小
//          			nFlags		读取标记
//  Output:         	pBuf		读取的数据
//  Return:         	返回读取的字节数，如返回-1则遇到错误
//////////////////////////////////////////////////////////////////////
int CSocket::RemoteRecv(void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);
	
	bytes_read = 0;					// Reset the byte counter
	int num_read = 0;				// Actual number of bytes read
	int num_req = nBytes;			// Number of bytes requested 
	char *pRecvBuf = (char *)pBuf;	// Pointer to the buffer
	
	while(bytes_read < nBytes) 
	{ 
		// Loop until the buffer is full

		num_read = recv(remote_socket, pRecvBuf, num_req-bytes_read, nFlags);

		if(num_read  > 0) 
		{
			bytes_read += num_read;   // Increment the byte counter
			pRecvBuf += num_read;     // Move the buffer pointer for the next read
		}
		else if(num_read == 0) 
		{ 
			// Gracefully closed
			socket_error = SOCKET_DISCONNECTED_ERROR;
			return -1; // An error occurred during the read
		}
		else 
		{
			socket_error = SOCKET_RECEIVE_ERROR;
			return -1; // An error occurred during the read
		}
	}
	
	return bytes_read;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int RemoteSend(const void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	向remote_socket写入数据，在所有的数据都被写入后返回。
//  Calls:           	无
//  Data Accessed: 		remote_socket
//  Data Updated:   	socket_error、bytes_write
//  Input:          	pBuf       写入的数据
//          			nBytes     写入数据大小
//          			nFlags     写入标记
//  Output:         	无
//  Return:         	返回写入的字节数，如返回-1则遇到错误
//////////////////////////////////////////////////////////////////////
int CSocket::RemoteSend(const void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_write = 0;				// Reset the byte counter
	int num_moved = 0;				// Actual number of bytes written
	int num_req = nBytes;			// Number of bytes requested 
	char* pSendBuf = (char*)pBuf;	// Pointer to the buffer
	
	while(bytes_write < nBytes) 
	{ 
		// Loop until the buffer is empty
		num_moved = send(remote_socket, pSendBuf, num_req-bytes_write, nFlags);
		if(num_moved > 0)
		{
			bytes_write += num_moved;  // Increment the byte counter
			pSendBuf += num_moved;     // Move the buffer pointer for the next read
		}
		else 
		{
			socket_error = SOCKET_TRANSMIT_ERROR;
			return -1; // An error occurred during the read
		}
	}
	
	return bytes_write;
}

void CSocket::SetTime(int _sec)
{
	struct timeval tv;
	tv.tv_usec = 0;	
	tv.tv_sec = _sec;
	
	setsockopt(local_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval));
	setsockopt(local_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(struct timeval));
	
}

// --------- End of File --------- //
