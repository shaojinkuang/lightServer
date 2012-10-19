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

//����socket������ʾ��Ϣ
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
//  Description:    	CSocket�Ĺ��캯���������ʼ��CSocket�ĳ�Ա����
//  Calls:           	Clear	//��ʼ����Ա����
//  Data Accessed: 		��
//  Data Updated:   	��
//  Input:          	��
//  Output:         	��
//  Return:         	��
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
//  Description:    	��ʼ����Ա������
//  Calls:           	��
//  Data Accessed: 		��
//  Data Updated:   	address_family��socket_type��protocol_family��port_number��local_ socket��remote_socket��bytes_read��bytes_moved��is_connected��is_bound��socket_error
//  Input:          	��
//  Output:         	��
//  Return:         	��
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
//  Description:    	CSocket����������������CSocket����Դ�ͷ�
//  Calls:           	Close	//�ر�����socket����
//  Data Accessed: 		��
//  Data Updated:   	��
//  Input:          	��
//  Output:         	��
//  Return:         	��
//////////////////////////////////////////////////////////////////////
CSocket::~CSocket()
{
	Close();
}

//////////////////////////////////////////////////////////////////////
//  Function:       	void Close();
//  Description:    	�ر�����Socket��
//  Calls:           	��
//  Data Accessed: 		��
//  Data Updated:   	bytes_moved��bytes_read��is_connected��is_bound��local_socket��remote_socket
//  Input:          	��
//  Output:         	��
//  Return:         	��
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
//  Description:    	��������ʼ��Socket��
//  Calls:           	��
//  Data Accessed: 		��
//  Data Updated:   	socket_type
//						protocol_family
//						address_family
//						port_number
//						local_sin
//						socket_error
//  Input:          	nSocketType	Socket���ͣ���SOCK_STREAM��SOCK_DGRAM����
//						nPort	  	�˿ں�
//						szHost		�������֣�������ַ�����Ϊ�գ����ʼ��SocketΪ�����Socket
//  Output:         	��
//  Return:         	��ʼ���ɹ���Socket ID���緵��-1���ʼ��ʧ��
//////////////////////////////////////////////////////////////////////
int CSocket::InitSocket(int nSocketType, int nPort, const char* szHost)
{
	//����socket_type��protocol_family��address_family��port_number

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
	
	//����local_sin
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

    //ȡ��������ַ
    int sock;
    for (res = res0; res; res = res->ai_next) {
        memcpy(&local_sin, res->ai_addr, res->ai_addrlen);
    }
    freeaddrinfo(res0);

	//local_sin.sin_port = htons(port_number); 
	
	//����socket
	local_socket = socket(address_family, socket_type, protocol_family);
	//���socket�����Ƿ�ɹ�
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

//    //����socket_type��protocol_family��address_family��port_number
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
//    //����local_sin
//    if (szHost == NULL)
//    {
//        local_sin.sin_addr.s_addr = INADDR_ANY;
//    }
//    else
//    {
//        //ȡ��������ַ
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
//    //����socket
//    local_socket = socket(address_family, socket_type, protocol_family);
//    //���socket�����Ƿ�ɹ�
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
//  Description:    	����Socket�����ӵ�SocketΪInitSocket�����г�ʼ����Socket��
//  Calls:           	��
//  Data Accessed: 		local_socket��local_sin
//  Data Updated:   	is_connected��socket_error
//  Input:          	��
//  Output:         	��
//  Return:         	����0��ɹ�������������Ϊ������
//  Others:         	α������
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
//  Description:    	����һ��Զ��Socket�����ӣ��ú���ֱ������һ��Զ��Socket���Ӳŷ��ء�
//  Calls:           	��
//  Data Accessed: 		remote_sin
//  Data Updated:   	socket_error��remote_socket
//  Input:          	��
//  Output:         	��
//  Return:         	���ӳɹ���Զ��Socket ID���緵��-1��ʧ��
//////////////////////////////////////////////////////////////////////
int CSocket::Accept()
{
	int addr_size = sizeof(remote_sin);
#if defined (__LINUX__)
	remote_socket = accept(local_socket, (struct sockaddr *)&remote_sin, (socklen_t*)&addr_size);
#else
    remote_socket = accept(local_socket, (struct sockaddr *)&remote_sin, &addr_size);
#endif
	//�ж�remote_socketֵ����ʧ�����趨socket_error
	if(remote_socket < 0)
    {
		socket_error = SOCKET_ACCEPT_ERROR;
		return -1;
    }
	
	return remote_socket;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int Bind();
//  Description:    	��Socket�Ա�������ݡ�
//  Calls:           	��
//  Data Accessed: 		local_socket��local_sin
//  Data Updated:   	is_bound��socket_error
//  Input:          	��
//  Output:         	��
//  Return:         	����0��ɹ�������������Ϊ������
//////////////////////////////////////////////////////////////////////
int CSocket::Bind()
{
	int rv = bind(local_socket, (struct sockaddr *)&local_sin, sizeof(local_sin));
	//�ж�rvֵ������is_bound����ʧ��������socket_error
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
//  Description:    	������óɷ����Socket�Ļ��������ӣ���������������ĿΪ����������������
//  Calls:           	��
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error
//  Input:          	nMaxConnections	���������
//  Output:         	��
//  Return:         	����0��ɹ�������������Ϊ������
//////////////////////////////////////////////////////////////////////
int CSocket::Listen(int nMaxConnections)
{
	int rv = listen(local_socket, nMaxConnections); 
	//�ж�rvֵ����ʧ��������socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_LISTEN_ERROR;
	}

	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int SetSockOpt(int nLevel, int nOptName, const void *pOptVal,  int nOptLen);
//  Description:    	����Socket�Ĳ�����
//  Calls:           	��
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error
//  Input:          	nLevel		Э���׼
//           			nOptName	�������
//           			pOptVal		����ָ��
//						uOptLen		��������
//  Output:         	��
//  Return:         	����0��ɹ�������������Ϊ������
//////////////////////////////////////////////////////////////////////
int CSocket::SetSockOpt(int nLevel, int nOptName, const void *pOptVal,  int nOptLen)
{
#if defined (__WIN32__)
	int rv = setsockopt(local_socket, nLevel, nOptName, (const char*)pOptVal, nOptLen);
#else
	int rv = setsockopt(local_socket, nLevel, nOptName, pOptVal, nOptLen);
#endif	
	//�ж�rvֵ����ʧ��������socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_SETOPTION_ERROR;
	}

	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int GetSockOpt(int nLevel, int nOptName, const void *pOptVal,  int* pOptLen);
//  Description:    	��ȡSocket�Ĳ�����
//  Calls:           	��
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error
//  Input:          	nLevel		Э���׼
//           			nOptName	�������
//  Output:         	pOptVal		����ָ��
//						pOptLen		��������
//  Return:         	����0��ɹ�������������Ϊ������
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
	//�ж�rvֵ����ʧ��������socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_GETOPTION_ERROR;
	}

	return rv;
}


//////////////////////////////////////////////////////////////////////
//  Function:       	int GetSockName(int nSocket, sockaddr_in* sa);
//  Description:    	��ȡSocket�Ĳ�����
//  Calls:           	��
//  Data Accessed: 	��
//  Data Updated:   	��
//  Input:          	nSocket	Socket ID
//  Output:         	sa		��ַ��Ϣ
//  Return:         	����0��ɹ�������������Ϊ������
//////////////////////////////////////////////////////////////////////
int CSocket::GetSockName(int nSocket, sockaddr_in* sa)
{
	int namelen = sizeof(sockaddr_in);
#if defined (__LINUX__)
	int rv = getsockname(nSocket, (struct sockaddr *)sa, (socklen_t*)&namelen);
#else
	int rv = getsockname(nSocket, (struct sockaddr *)sa, &namelen);
#endif
	//�ж�rvֵ����ʧ��������socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_SOCKNAME_ERROR;
	}

	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int GetHostName(char *szBuf);
//  Description:    	��ȡ�������֡�
//  Calls:           	��
//  Data Accessed: 		��
//  Data Updated:   	socket_error
//  Input:          	��
//  Output:         	szBuf		��������
//  Return:         	����0��ɹ�������������Ϊ������
//////////////////////////////////////////////////////////////////////
int CSocket::GetHostName(char *szBuf)
{
	if (szBuf == NULL) 
	{
		return -1;
	}
	//����gethostname��ȡ��������
	int rv = gethostname(szBuf, MAX_NAME_LEN);
	//�жϷ���ֵ����ʧ��������socket_error
	if(rv < 0) 
	{
		socket_error = SOCKET_HOSTNAME_ERROR;
	}
	
	return rv;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	hostent GetHostInformation(char *szHostName);
//  Description:    	��ȡ������Ϣ��
//  Calls:           	��
//  Data Accessed: 		��
//  Data Updated:   	socket_error
//  Input:          	szHostName	��������
//  Output:         	��
//  Return:         	������Ϣ
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
//  Description:    	����Socket������롣
//  Calls:           	��
//  Data Accessed: 		��
//  Data Updated:   	socket_error
//  Input:          	err	�������
//  Output:         	��
//  Return:         	�������
//////////////////////////////////////////////////////////////////////
SocketError CSocket::SetSocketError(SocketError err)
{
	return socket_error = err;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	SocketError GetSocketError();
//  Description:    	��ȡSocket������롣
//  Calls:           	��
//  Data Accessed: 		socket_error 
//  Data Updated:   	��
//  Input:          	szHostName	��������
//  Output:         	��
//  Return:         	�������
//////////////////////////////////////////////////////////////////////
SocketError CSocket::GetSocketError()
{
	return socket_error;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	const char *gxSocket::SocketExceptionMessage();
//  Description:    	��ȡSocket����������ʾ��Ϣ��
//  Calls:           	��
//  Data Accessed: 		socket_error 
//  Data Updated:   	��
//  Input:          	��
//  Output:         	��
//  Return:         	����������ʾ��Ϣ
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
//  Description:    	��local_socket��ȡ���ݣ����������е����ݱ�����ǰ���ء�
//  Calls:           	��
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error��bytes_read
//  Input:          	pBuf        ������ָ��
//          			nBytes		��������С
//          			nFlags		��ȡ���
//  Output:         	pBuf		��ȡ������
//  Return:         	���ض�ȡ���ֽ������緵��-1����������
//////////////////////////////////////////////////////////////////////
int CSocket::RawRead(void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_read = 0;
	bytes_read = recv(local_socket, (char *)pBuf, nBytes, nFlags);
	//�ж�bytes_read�������������socket_error
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
//  Description:    	��local_socketд�����ݣ����������е����ݱ�д��ǰ���ء�
//  Calls:           	��
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error��bytes_write
//  Input:          	pBuf        д�������
//          			nBytes		д�����ݴ�С
//          			nFlags		д����
//  Output:         	��
//  Return:         	����д����ֽ������緵��-1����������
//////////////////////////////////////////////////////////////////////
int CSocket::RawWrite(const void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_write = 0;
	bytes_write = send(local_socket, (char *)pBuf, nBytes, nFlags);
	//�ж�bytes_write�������������socket_error
	if(bytes_write < 0) 
	{
		socket_error = SOCKET_TRANSMIT_ERROR;
	}

	return bytes_write;
}

//////////////////////////////////////////////////////////////////////
//  Function:       	int RawRemoteRead(void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	��remote_socket��ȡ���ݣ����������е����ݱ�����ǰ���ء�
//  Calls:           	��
//  Data Accessed: 		remote_socket
//  Data Updated:   	socket_error��bytes_read
//  Input:          	pBuf        ������ָ��
//          			nBytes		��������С
//          			nFlags		��ȡ���
//  Output:         	pBuf		��ȡ������
//  Return:         	���ض�ȡ���ֽ������緵��-1����������
//////////////////////////////////////////////////////////////////////
int CSocket::RawRemoteRead(void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_read = 0;
	bytes_read = recv(remote_socket, (char *)pBuf, nBytes, nFlags);
	//�ж�bytes_read�������������socket_error
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
//  Description:    	��remote_socketд�����ݣ����������е����ݱ�д��ǰ���ء�
//  Calls:           	��
//  Data Accessed: 		remote_socket
//  Data Updated:   	socket_error��bytes_write
//  Input:          	pBuf        д�������
//          			nBytes		д�����ݴ�С
//          			nFlags		д����
//  Output:         	��
//  Return:         	����д����ֽ������緵��-1����������
//////////////////////////////////////////////////////////////////////
int CSocket::RawRemoteWrite(const void *pBuf, int nBytes, int nFlags)
{
	assert(pBuf != NULL);

	bytes_write = 0;
	bytes_write = send(remote_socket, (char *)pBuf, nBytes, nFlags);
	//�ж�bytes_write�������������socket_error
	if(bytes_write < 0) 
	{
		socket_error = SOCKET_TRANSMIT_ERROR;
	}

	return bytes_write;
}


//////////////////////////////////////////////////////////////////////
//  Function:       	int WaitMsg( int nSeconds );
//  Description:    	����ǰ��ʱ�ж�
//  Calls:           	��
//  Data Accessed: 		local_socket
//  Input:          	nSeconds        ��ʱʱ��
//  Output:         	
//  Return:         	����0��ʾ�ɹ���-1��ʾ��ʱ
//////////////////////////////////////////////////////////////////////

int CSocket::WaitMsg( int nSeconds )
{
    //���峬ʱʱ��
    struct timeval timeout;
    timeout.tv_sec  = nSeconds;
    timeout.tv_usec = 0;
    //��ʼ���ȴ�socket�¼�����
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET( local_socket, &fds);

    //�ȴ���Ӧ
    if( select(local_socket+1, &fds, 0, 0, &timeout) == 0 ) 
    { 
    	//����socket������
    	socket_error = SOCKET_REQUEST_TIMEOUT;      
    	return -1;
    }

    return 0;
}


//////////////////////////////////////////////////////////////////////
//  Function:       	int Recv(void *pBuf, int nBytes, int nFlags = 0);
//  Description:    	��local_socket��ȡ���ݣ������е����ݶ������պ󷵻ء�
//  Calls:           	��
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error��bytes_read
//  Input:          	pBuf        ������ָ��
//          			nBytes		��������С
//          			nFlags		��ȡ���
//  Output:         	pBuf		��ȡ������
//  Return:         	���ض�ȡ���ֽ������緵��-1����������
//////////////////////////////////////////////////////////////////////
int CSocket::Recv(void *pBuf, int nBytes, int nSeconds, int nFlags )
{
	assert(pBuf != NULL);
	
	bytes_read = 0;			// Reset the byte counter
	int num_read = 0;		// Actual number of bytes read
	int num_req = nBytes;		// Number of bytes requested 
	char *pRecvBuf = (char *)pBuf;	// Pointer to the buffer

	//���峬ʱʱ��
	struct timeval timeout;
	timeout.tv_sec  = nSeconds;
	timeout.tv_usec = 0;
	//��ʼ���ȴ�socket�¼�����
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET( local_socket, &fds);
	
	while( bytes_read < nBytes ) 
	{ 
		//�ȴ���Ӧ
		if( select(local_socket+1, &fds, 0, 0, &timeout) == 0 ) 
		{ 
			//����socket������
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
//  Description:    	��local_socketд�����ݣ������е����ݶ���д��󷵻ء�
//  Calls:           	��
//  Data Accessed: 		local_socket
//  Data Updated:   	socket_error��bytes_write
//  Input:          	pBuf        д�������
//          			nBytes		д�����ݴ�С
//          			nFlags		д����
//  Output:         	��
//  Return:         	����д����ֽ������緵��-1����������
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
//  Description:    	��remote_socket��ȡ���ݣ������е����ݶ������պ󷵻ء�
//  Calls:           	��
//  Data Accessed: 		remote_socket
//  Data Updated:   	socket_error��bytes_read
//  Input:          	pBuf        ������ָ��
//          			nBytes		��������С
//          			nFlags		��ȡ���
//  Output:         	pBuf		��ȡ������
//  Return:         	���ض�ȡ���ֽ������緵��-1����������
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
//  Description:    	��remote_socketд�����ݣ������е����ݶ���д��󷵻ء�
//  Calls:           	��
//  Data Accessed: 		remote_socket
//  Data Updated:   	socket_error��bytes_write
//  Input:          	pBuf       д�������
//          			nBytes     д�����ݴ�С
//          			nFlags     д����
//  Output:         	��
//  Return:         	����д����ֽ������緵��-1����������
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
