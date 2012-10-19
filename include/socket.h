#if !defined(RBI_SOCKET_H)
#define RBI_SOCKET_H

// --------------------------------------------------------------
// Platform specific include files
// --------------------------------------------------------------
#if defined (__UNIX__)
#include <unistd.h>     // UNIX standard function definitions 
#include <sys/types.h>  // Type definitions
#include <netdb.h>      // Net DB structures
#include <arpa/inet.h>  // Inet functions
#include <netinet/in.h> // Structures defined by the internet system
#include <sys/socket.h> // Definitions related to sockets
#include <sys/time.h>   // Time value functions

#elif defined (__LINUX__)
#include <unistd.h>     // UNIX standard function definitions 
#include <sys/types.h>  // Type definitions
#include <netdb.h>      // Net DB structures
#include <arpa/inet.h>  // Inet functions
#include <netinet/in.h> // Structures defined by the internet system
#include <sys/socket.h> // Definitions related to sockets
#include <sys/time.h>   // Time value functions

#elif defined (__WIN32__)
#include <winsock.h>	// Windows socket functions
#include <time.h>		// Time value functions

#else
//#error You must define a target platform:\n __UNIX__ or __LINUX__

#endif
// --------------------------------------------------------------

//Socket通讯的返回码
enum SocketError {    			// Socket exception codes
	SOCKET_NO_ERROR = 0,       	// No socket errors reported
	SOCKET_INVALID_ERROR_CODE, 	// Invalid socket error code
	
	// Socket error codes
	SOCKET_ACCEPT_ERROR,        // Error accepting remote socket
	SOCKET_BIND_ERROR,          // Could not bind socket
	SOCKET_BUFOVER_ERROR,       // Buffer overflow 
	SOCKET_CONNECT_ERROR,       // Could not connect socket
	SOCKET_DISCONNECTED_ERROR, 	// Socket has been disconnected
	SOCKET_FILELENGTH_ERROR,    // File length does not match expected value
	SOCKET_FILEMODTIME_ERROR,   // File modification time dates do not match
	SOCKET_FILESYSTEM_ERROR,    // A file system error occurred
	SOCKET_GETOPTION_ERROR,     // Error getting socket option
	SOCKET_HOSTNAME_ERROR,      // Could not resolve hostname
	SOCKET_INIT_ERROR,          // Initialization error
	SOCKET_LISTEN_ERROR,        // Listen error
	SOCKET_PEERNAME_ERROR,      // Get peer name error
	SOCKET_PROTOCOL_ERROR,      // Unknown protocol requested
	SOCKET_RECEIVE_ERROR,       // Receive error
	SOCKET_REQUEST_TIMEOUT,     // Request timed out
	SOCKET_SERVICE_ERROR,       // Unknown service requested
	SOCKET_SETOPTION_ERROR,     // Error setting socket option
	SOCKET_SOCKNAME_ERROR,      // Get socket name error
	SOCKET_SOCKETTYPE_ERROR,    // Unknown socket type requested
	SOCKET_TRANSMIT_ERROR,      // Transmit error
	
	// Exception codes added to handle database block errors
	SOCKET_BLOCKACK_ERROR,		// Database block acknowledgment error
	SOCKET_BLOCKHEADER_ERROR,	// Bad database block header
	SOCKET_BLOCKSIZE_ERROR,		// Bad database block size
	SOCKET_BLOCKSYNC_ERROR,		// Database block synchronization error
	
	// Exception codes added to handle embedded protocol errors
	SOCKET_FTP_ERROR			// FTP protocol reported error condition
};

enum SocketServices {
	// Common port assignments
	SOCKET_ECHO_PORT       = 7,		// Echo port
	SOCKET_FTPDATA_PORT    = 20,	// FTP data port
	SOCKET_FTP_PORT        = 21,	// FTP port
	SOCKET_TELNET_PORT     = 23,	// Telnet port
	SOCKET_SMTP_PORT       = 25,	// Simple mail transfer protocol port
	SOCKET_TIME_PORT       = 37,	// Time server
	SOCKET_NAME_PORT       = 42,	// Name server
	SOCKET_NAMESERVER_PORT = 53,	// Domain name server
	SOCKET_FINGER_PORT     = 79,	// Finger port
	SOCKET_HTTP_PORT       = 80,	// HTTP port
	SOCKET_POP_PORT        = 109,	// Postoffice protocol
	SOCKET_POP2_PORT       = 109,	// Postoffice protocol
	SOCKET_POP3_PORT       = 110,	// Postoffice protocol
	SOCKET_NNTP_PORT       = 119,	// Network news transfer protocol
	
	SOCKET_DBS_PORT		 = 2073,	// gxs-data-port (services file name)
	SOCKET_DEFAULT_PORT	 = 2073		// Default port if no port number is specified
};

const unsigned MAX_NAME_LEN =  256;  // Maximum string name length


//CSocket class 
class CSocket
{
public:
	CSocket(); 
	virtual ~CSocket();
	
public: // socket 操作函数
	//初始化socket
	int InitSocket(int nSocketType, int nPort, const char* szHost = NULL);
	//连接socket
	int Connect();
	//接受远端socket连接
	int Accept();
	//绑定socket
	int Bind();
	//监听socket
	int Listen(int nMaxConnections);
	//关闭socket
	void Close();
	//设置socket参数
	int SetSockOpt(int nLevel, int nOptName, const void *pOptVal,  int nOptLen);
	//获取socket参数
	int GetSockOpt(int nLevel, int nOptName, void *pOptVal,  int* pOptLen);
	//获取socket名称
	int GetSockName(int nSocket, sockaddr_in* sa);
	//获取主机名字
	int GetHostName(char *szBuf);
	//获取主机信息
	hostent* GetHostInformation(char *szHostName);
	
	//从local_socket读取数据
	int RawRead(void *pBuf, int nBytes, int nFlags = 0);
	//向local_socket写入数据
	int RawWrite(const void *pBuf, int nBytes, int nFlags = 0);
	//从remote_socket读取数据
	int RawRemoteRead(void *pBuf, int nBytes, int nFlags = 0);
	//向remote_socket写入数据
	int RawRemoteWrite(const void *pBuf, int nBytes, int nFlags = 0);
	//超时判断
	int WaitMsg( int nSeconds );
	//从local_socket读取数据
	int Recv(void *pBuf, int nBytes, int nSeconds, int nFlags = 0 );
	//向local_socket写入数据
	int Send(const void *pBuf, int nBytes, int nFlags = 0);
	//从remote_socket读取数据
	int RemoteRecv(void *pBuf, int nBytes, int nFlags = 0);
	//向remote_socket写入数据
	int RemoteSend(const void *pBuf, int nBytes, int nFlags = 0);
	
	//设置错误码
	SocketError SetSocketError(SocketError err);
	//取得错误码
	SocketError GetSocketError();
	//取得socket异常信息提示
	const char *SocketExceptionMessage();

	void SetTime(int _sec);
protected:
	//初始化成员变量
	void Clear();

public:
	int local_socket;           // 本地socket
	int remote_socket;         	// 接受连入的远端socket
protected: 
	int address_family;   		// socket地址
	int protocol_family; 		// socket协议类型
	int socket_type;           	// socket 类型
	int port_number;            // socket端口
	SocketError socket_error;   // 最后一次socket错误码
	
protected: 
	int bytes_read;   			// 读取socket的字节数
	int bytes_write;  			// 写入socket的字节数
	int is_connected; 			// socket是否连接标记
	int is_bound;     			// socket是否绑定标记
	
public: 
	sockaddr_in local_sin;  	// 本地socket地址
	sockaddr_in remote_sin; 	// 远端socket地址
};


#endif //RBI_SOCKET_H

// --------- End of File --------- //
