#include "stdafx.h"
#include "netcomm.h"
using namespace netcomm;
//////////////////////////////////////////////////////////////////////////
/**
  @fn BOOL netcomm::InitSock(BYTE minVer, BYTE maxVer)

  *  初始化winsock库

  @param minVer 次版本号
  @param maxVer 主版本号
  @return       TRUE表示初始化成功, FALSE表示失败
  */
//////////////////////////////////////////////////////////////////////////
bool netcomm::InitSock(BYTE minVer, BYTE maxVer)
{
	WSADATA  WsaData;
	if (0 != WSAStartup(MAKEWORD(maxVer, minVer), &WsaData))
	{
		return false;
	}
	return true;
}
//////////////////////////////////////////////////////////////////////////
/**
   * 释放清除winsock库
*/
//////////////////////////////////////////////////////////////////////////
void netcomm::CleanSock()
{
	WSACleanup();
}
//////////////////////////////////////////////////////////////////////////
/**
   *  获得本地主机IP

   @return 本地主机IP字符串
*/
//////////////////////////////////////////////////////////////////////////
char* netcomm::GetLocalHostIP()
{
	char szHost[256] ;
	if (SOCKET_ERROR == gethostname(szHost, 256))
	{
		return NULL;
	}
	hostent *pHost = gethostbyname(szHost);
	if (NULL == pHost)
	{
		return NULL;
	}
	in_addr addr;
	memcpy(&addr.S_un.S_addr, pHost->h_addr, pHost->h_length);
	return inet_ntoa(addr);
}
//////////////////////////////////////////////////////////////////////////
/**
    * 16位校验和算法

	@param buffer  存放二进制数据的缓冲区
	@param size    数据长度，字节数
	@return        校验和结果
*/
//////////////////////////////////////////////////////////////////////////
unsigned short netcomm::checksum(unsigned short *buffer, int size)
{
	unsigned long cksum=0;

	while (size > 1) 
	{
		cksum += *buffer++;
		size -= sizeof(unsigned short);
	}
	if (size) 
	{
		cksum += *(UCHAR*)buffer;
	}
	cksum = (cksum >> 16) + (cksum & 0xffff);
	cksum += (cksum >>16);
	return (unsigned short)(~cksum);
}

/*****************************************************************************************
解析目的地址
入口
strDest : 目的地址
出口
若解析成功,返回对应的addrinfo指针; 否则返回NULL
*****************************************************************************************/
addrinfo* netcomm::ResolveAddress(const string& strDest)
{
	struct addrinfo hints,	*res = NULL;
	int             rc;
	const char* pnodename = strDest.c_str();
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags  = ( pnodename ? 0 : AI_PASSIVE);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = 0;
	hints.ai_protocol = 0;

	rc = getaddrinfo(pnodename, NULL, &hints, &res);
	if (rc != 0)
	{
		printf("Invalid address %s, getaddrinfo failed: %d\n", pnodename, rc);
		return NULL;
	}
	return res;
}

/************************************************************************
    
************************************************************************/
bool netcomm::IsIPAddrEqual(const sockaddr& sa1, const sockaddr& sa2)
{
	return ((sockaddr_in*)&sa1)->sin_addr.s_addr == ((sockaddr_in*)&sa2)->sin_addr.s_addr;
}

/*******************************************************************************
  @brief 
  
  *
*******************************************************************************/

//////////////////////////////////////////////////////////////////////////
/**
	@brief 使能保活功能
*/
//////////////////////////////////////////////////////////////////////////
bool netcomm::EnableKeepAlive(SOCKET s, const TCP_KEEPALIVE& tcp_keepalive)
{
	ASSERT(tcp_keepalive.onoff == 1);

	DWORD dwInLen = sizeof(tcp_keepalive);
	DWORD dwOutLen = dwInLen;
	TCP_KEEPALIVE  OutKeepAlive = { 0 };
	DWORD dwBytesRet;

	if (WSAIoctl(s, SIO_KEEPALIVE_VALS,   
		(LPVOID)&tcp_keepalive, dwInLen,   
		&OutKeepAlive, dwOutLen,   
		&dwBytesRet, NULL, NULL) == SOCKET_ERROR)  
	{
        TRACE("WSAIoctl Failed with Error: %d \n", WSAGetLastError());
		return false;
	} 
	return true;
}

//////////////////////////////////////////////////////////////////////////
/**
	@brief 禁用保活功能
*/
//////////////////////////////////////////////////////////////////////////
bool netcomm::DisableKeepAlvie(SOCKET s)
{
	TCP_KEEPALIVE tcp_keepalive = { 0 };
	DWORD dwInLen = sizeof(tcp_keepalive);
	DWORD dwOutLen = dwInLen;
	TCP_KEEPALIVE  OutKeepAlive = { 0 };
	DWORD dwBytesRet;

	if (WSAIoctl(s, SIO_KEEPALIVE_VALS,   
		(LPVOID)&tcp_keepalive, dwInLen,   
		&OutKeepAlive, dwOutLen,   
		&dwBytesRet, NULL, NULL) == SOCKET_ERROR)  
	{
		TRACE("WSAIoctl Failed with Error: %d \n", WSAGetLastError());
		return false;
	} 
	return true;
}
