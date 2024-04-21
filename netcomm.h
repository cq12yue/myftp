#ifndef _NETCOMM_H
#define _NETCOMM_H

//////////////////////////////////////////////////////////////////////////
	/**
	@namespace netcomm
	@brief 网络命名空间，表示网络通讯相关模块

	* 包括了常用的API及类, 目前封装的类如下 \n
	@class CTCPNet \n
	@class CUDPNet \n
	@class CPing \n
	@class CTrace \n
	@class CFtp   
	*/
//////////////////////////////////////////////////////////////////////////

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
using namespace std;
#pragma comment (lib,"ws2_32.lib")

namespace netcomm 
{
	/************************************************************************
	ICMP协议报文头
	************************************************************************/
	// ICMP types and codes
    #define ICMPV4_ECHO_REQUEST_TYPE   8
    #define ICMPV4_ECHO_REQUEST_CODE   0
    #define ICMPV4_ECHO_REPLY_TYPE     0
    #define ICMPV4_ECHO_REPLY_CODE     0
    #define ICMPV4_MINIMUM_HEADER      8
    #define ICMPV4_TIMEOUT            11 

    #pragma  pack(1)
	typedef struct _ICMP_HEADER
	{
		/************************************************************************
		请求头
		************************************************************************/ 
		unsigned char   icmp_type;		// 消息类型
		unsigned char   icmp_code;		// 代码
		unsigned short  icmp_checksum;	// 校验和
		/************************************************************************
		回显头
		************************************************************************/ 
		unsigned short  icmp_id;		// 用来惟一标识此请求的ID号，通常设置为进程ID
		unsigned short  icmp_sequence;	// 序列号
		unsigned long   icmp_timestamp; // 时间戳 (发送)
	}ICMP_HEADER, *PICMP_HEADER;
	typedef struct _IP_HEADER		// 20字节的IP头
	{
		unsigned char    iphVerLen;      // 版本号和头长度（各占4位）
		unsigned char    ipTOS;          // 服务类型 
		unsigned short   ipLength;       // 封包总长度，即整个IP报的长度
		unsigned short   ipID;			 // 封包标识，惟一标识发送的每一个数据报
		unsigned short   ipFlags;	     // 标志
		unsigned char    ipTTL;	         // 生存时间，就是TTL
		unsigned char    ipProtocol;     // 协议，可能是TCP、UDP、ICMP等
		unsigned short   ipChecksum;     // 校验和
		unsigned long    ipSource;       // 源IP地址
		unsigned long    ipDestination;  // 目标IP地址
	} IP_HEADER, *PIP_HEADER; 
    #pragma pack()

	//////////////////////////////////////////////////////////////////////////
	/**
	   @brief 保活定时结构
	*/
	//////////////////////////////////////////////////////////////////////////
	struct TCP_KEEPALIVE
	{ 
		u_long  onoff; ///< 是否启动保活
		u_long  keepalivetime; ///< 保活时间,以毫秒为单位 
		u_long  keepaliveinterval; ///< 探测时间间隔
	} ; 
    //////////////////////////////////////////////////////////////////////////
    /**
       @brief 定义保活选项值
    */
    //////////////////////////////////////////////////////////////////////////
    #define SIO_KEEPALIVE_VALS   _WSAIOW(IOC_VENDOR,4) 
	
	bool EnableKeepAlive(SOCKET s, const TCP_KEEPALIVE& tcp_keepalive);
	bool DisableKeepAlvie(SOCKET s);

	static const DWORD IP_HEADER_SIZE = sizeof(IP_HEADER);
	static const DWORD ICMP_HEADER_SIZE = sizeof(ICMP_HEADER); /* = 12 */
    static const DWORD ICMP_PACKET_SIZE = ICMP_HEADER_SIZE + 32;
	
	/************************************************************************
	          some API functions about netcomm             
	************************************************************************/
	bool  InitSock(BYTE minVer, BYTE maxVer) ;
    void  CleanSock();
	char* GetLocalHostIP();
    unsigned short checksum(unsigned short *buffer, int size); 
	addrinfo* ResolveAddress(const string& strDest);
	bool IsIPAddrEqual(const sockaddr& sa1, const sockaddr& sa2);

	inline void SAFE_CLOSE_SOCKET(SOCKET s)
	{
		if (INVALID_SOCKET != s)
		{
			closesocket(s); s = INVALID_SOCKET;
		}
	}

	/************************************************************************
       Ping类:    该类的实现在ping.h, ping.cpp文件中
	************************************************************************/
	class CPing;           
	
	/************************************************************************
	  TCP网络通讯类: 该类的实现在tcpnet.h, tcpnet.cpp文件中 
	************************************************************************/
	class CTCPNet;    

	/************************************************************************
	  UDP网络通讯类: 该类的实现在udpnet.h, udpnet.cpp文件中
	*************************************************************************/
    class CUDPNet;

	/************************************************************************
       路由跟踪类: 该类的实现在trace.h, trace.cpp文件中
	************************************************************************/
	class CTrace;

	/*************************************************************************
	  文件和目录传送类:  该类的实现在ftp.h, ftp.cpp文件中
	**************************************************************************/
	class CFtp;

}

#endif
