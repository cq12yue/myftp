#include "stdafx.h"
#include "tcpnet.h"
using namespace netcomm;

//////////////////////////////////////////////////////////////////////////
//CSendTask实现
CTCPNet::CSendTask::CSendTask(const CTCPNetBuf& tcpnetBuf, CTCPNet* pSender, bool bSendLen):
m_tcpnetBuf(tcpnetBuf),
m_pSender(pSender),
m_bSendLen(bSendLen)
{
    
}

CTCPNet::CSendTask::~CSendTask()
{

}

//////////////////////////////////////////////////////////////////////////
//CListenThread实现
CTCPNet::CListenThread::CListenThread(CTCPNet* pListener):
m_pListener(pListener),
m_sListen(INVALID_SOCKET)
{

}

CTCPNet::CListenThread::~CListenThread()
{
    Stop();
}

bool CTCPNet::CListenThread::Start(WORD wPort)
{
	if (INVALID_SOCKET != m_sListen)
	{
		return true;
	}
	m_sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == m_sListen)
	{
		TRACE("create listen socket failed! \n");
		goto clean;
	}

	SOCKADDR_IN si;
	si.sin_family = AF_INET;
	si.sin_port = ::ntohs(wPort);
	si.sin_addr.S_un.S_addr = INADDR_ANY;
	if(::bind(m_sListen, (sockaddr*)&si, sizeof(si)) == SOCKET_ERROR)
	{
		TRACE("bind fail, error = %d \n", WSAGetLastError());
		goto clean;
	}

	if (SOCKET_ERROR == ::listen(m_sListen, 100))
	{
		TRACE("listen fail, error = %d \n", WSAGetLastError());
		goto clean;
	}
	if (!Create())
	{
		goto clean;
	}
	return true;
clean:
	SAFE_CLOSE_SOCKET(m_sListen); m_sListen = INVALID_SOCKET;
	return false;
}

void CTCPNet::CListenThread::Stop()
{
    if (INVALID_SOCKET != m_sListen) Exit();
}

void CTCPNet::CListenThread::Signal()
{
    SAFE_CLOSE_SOCKET(m_sListen); 
	m_sListen = INVALID_SOCKET; 
}

bool CTCPNet::CListenThread::Init()
{
	return true;
}

void CTCPNet::CListenThread::Run()
{
	TRACE("ListenThread is running... \n");

	for(SOCKET sClient;;)
	{
		int addrlen = sizeof(sockaddr_in);
		sockaddr_in clientaddr;
		sClient = accept(m_sListen, (SOCKADDR*)&clientaddr, &addrlen);
		if (INVALID_SOCKET == sClient || SOCKET_ERROR == sClient)
		{
			break;
		}
		////保活探测,以检测连接非正常中断,如对方主机崩溃,路由器故障等
		TCP_KEEPALIVE tcp_keepalive = { 1, 5000, 5000 };
		EnableKeepAlive(sClient, tcp_keepalive);

		string strIP = inet_ntoa(clientaddr.sin_addr);
		WORD   wPort = ntohs(clientaddr.sin_port);
		CRecvThread* pRecvThread = new CRecvThread(m_pListener, sClient, strIP, wPort);
		if (!pRecvThread)
		{
			closesocket(sClient);
		}
		else if (!pRecvThread->Create())
		{
			closesocket(sClient);
			delete pRecvThread;
		}
		else
		{
			m_pListener->m_thlist_recv->AddThread(pRecvThread);
		}
	}

	TRACE("ListenThread is exit... \n");
}

DWORD CTCPNet::CListenThread::Clean()
{
    return 0;
}

//////////////////////////////////////////////////////////////////////////
//CSendThread实现
CTCPNet::CSendThread::CSendThread(const string& strIP, WORD wPort):
m_h_task_not_empty(CreateEvent(NULL, FALSE, FALSE, NULL)),
m_hExit(CreateEvent(NULL, FALSE, FALSE, NULL)),
m_sSend(INVALID_SOCKET),
m_bConnected(false),
m_strIP(strIP),
m_wPort(wPort)
{
   
}

CTCPNet::CSendThread::~CSendThread()
{
  Exit();
  CloseHandle(m_h_task_not_empty);
  CloseHandle(m_hExit);
}

bool CTCPNet::CSendThread::Task_Eequeue_Tail(CSendTask* pSendTask)
{
	ASSERT(pSendTask);
	size_t count;
	{
		CAutoLockEx AutoLock(m_cs_task);
		try
		{
			m_deq_send.push_back(pSendTask);
		}
		catch (exception& e)
		{
			TRACE(_T("%s exception: %s \n"), typeid(e).name(), e.what()); 
			return false;
		}
		count = m_deq_send.size();
	}
	if (1 == count)
	{
		TRACE("SetEvent: task is not empyt \n");
		SetEvent(m_h_task_not_empty);
	}
	return true;
}

CTCPNet::CSendTask* CTCPNet::CSendThread::Task_Dequeue_Head()
{
	CAutoLockEx AutoLock(m_cs_task);
	ASSERT(!m_deq_send.empty());
	CSendTask* pSendTask = m_deq_send.front();
	m_deq_send.pop_front();
	return pSendTask;
}

inline bool CTCPNet::CSendThread::Task_Is_Empty()
{ 
	CAutoLockEx lock(m_cs_task);
	return m_deq_send.empty();
}

DWORD CTCPNet::CSendThread::WaitTask(DWORD dwTimeout /* = INFINITE */)
{
	HANDLE hEvents[] = { m_hExit, m_h_task_not_empty };
	DWORD dwIndex = WaitForMultipleObjects(NUM_ELEMENTS(hEvents), hEvents, FALSE, dwTimeout);
	if (WAIT_OBJECT_0 + 1 == dwIndex)
		return 0;
	else if (WAIT_OBJECT_0 == dwIndex)
		return -1;
	else 
		return WAIT_TIMEOUT;
}

void CTCPNet::CSendThread::SendTask(CSendTask* pSendTask)
{
	ASSERT(pSendTask);
    if (INVALID_SOCKET == m_sSend)
    {
	  m_sSend = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	  if (INVALID_SOCKET == m_sSend) return ;
	}
	if (!m_bConnected) 
	{
		m_bConnected = CTCPNet::Connect(m_sSend, m_strIP, m_wPort, Delay);
	}
	if (!m_bConnected)  
	{
		pSendTask->m_pSender->OnSendError(WSAGetLastError(), m_strIP, m_wPort);
		pSendTask->Release();
		return ;
	}
	if (!CTCPNet::SendData(m_sSend, pSendTask->m_tcpnetBuf.get(), pSendTask->m_tcpnetBuf.getlen(), Delay))
	{
	   closesocket(m_sSend); m_sSend = INVALID_SOCKET; m_bConnected = false;
	   pSendTask->m_pSender->OnSendError(WSAGetLastError(), m_strIP, m_wPort);
	   pSendTask->Release();
	   return ;
	}
	pSendTask->m_pSender->OnSendComplete(pSendTask->m_tcpnetBuf, pSendTask->m_tcpnetBuf.getlen(), m_strIP, m_wPort);
  	pSendTask->Release();
}

void CTCPNet::CSendThread::Signal()
{
	SAFE_CLOSE_SOCKET(m_sSend);
    SetEvent(m_hExit);
}

bool CTCPNet::CSendThread::Init()
{
  	return true;
}

void CTCPNet::CSendThread::Run()
{
	TRACE("SendThread %d is runing...... \n", GetCurrentThreadId());
	for (DWORD dwRet;;)
	{
		while (Task_Is_Empty())
		{
			dwRet = WaitTask();
			if (0 == dwRet || WAIT_TIMEOUT == dwRet)
			{
				continue;            
			}
			else if (-1 == dwRet)
			{
				TRACE("SendThread %d is exit...... \n", GetCurrentThreadId());
				return;   
			}
		}
		if (WAIT_OBJECT_0 == WaitForSingleObject(m_hExit, 0))
		{
			break;
		}
		SendTask(Task_Dequeue_Head());
	}
	TRACE("SendThread %d is exit...... \n", GetCurrentThreadId());
}

DWORD CTCPNet::CSendThread::Clean()
{
  for (deque<CSendTask*>::iterator iter = m_deq_send.begin(); iter != m_deq_send.end(); )
  {
	  (*iter)->Release();  iter = m_deq_send.erase(iter);
  }
  return 0;
}

//////////////////////////////////////////////////////////////////////////
//CSendThreadList实现
CTCPNet::CSendThread* CTCPNet::CSendThreadList::GetThread(const string& strIP, WORD wPort)
{
   CAutoLockEx lock(m_cs_list);
   for (list<CSendThread*>::iterator iter = m_list_send.begin(); iter != m_list_send.end(); ++iter)
   {
	   if (0 == (*iter)->m_strIP.compare(strIP) && (*iter)->m_wPort == wPort)
		   return *iter;
   }
   return 0;
}

CTCPNet::CSendThread* CTCPNet::CSendThreadList::NewThread(const string& strIP, WORD wPort)
{
   CAutoLockEx lock(m_cs_list);
   CSendThread* pSendThread = GetThread(strIP, wPort);
   if (!pSendThread)
   {
	   pSendThread = new CSendThread(strIP, wPort);
	   if (!pSendThread || !pSendThread->Create())
		   return 0;
	   m_list_send.push_back(pSendThread);
   }
  return pSendThread;
}

void CTCPNet::CSendThreadList::DeleteThread(CSendThread* pSendThread)
{
   CAutoLockEx lock(m_cs_list);
   m_list_send.remove(pSendThread);
}

void CTCPNet::CSendThreadList::ClearAllThread()
{
	CAutoLockEx lock(m_cs_list);
	for (list<CSendThread*>::iterator iter = m_list_send.begin(); iter != m_list_send.end(); )
	{
		delete (*iter); iter = m_list_send.erase(iter);
	}
}
//////////////////////////////////////////////////////////////////////////
//CRecvThread实现
CTCPNet::CRecvThread::CRecvThread(CTCPNet* pReceiver, SOCKET s, const string& strIP, WORD wPort):
m_pReceiver(pReceiver),
m_sRecv(s),
m_strIP(strIP),
m_wPort(wPort)
{

}

CTCPNet::CRecvThread::~CRecvThread()
{
   Exit();
}

void CTCPNet::CRecvThread::Signal()
{
   SAFE_CLOSE_SOCKET(m_sRecv);
}

bool CTCPNet::CRecvThread::Init()
{
   return true;
}

void CTCPNet::CRecvThread::Run()
{
	TRACE("RecvThread %d is running... \n", GetCurrentThreadId());

	char buf[RECV_BUF_LEN] = { 0 }; 
	for (DWORD dwRecv = 4, dwDataLen = 0, dwTotalLen = 0; ;)
	{
		if (!CTCPNet::RecvData(m_sRecv, buf, dwRecv))
		{
			m_pReceiver->OnReceiveError(WSAGetLastError(), m_strIP, m_wPort);
			break;
		}
		if (0 == dwTotalLen)
		{
			ASSERT(dwRecv == 4);
			memcpy(&dwDataLen, buf, dwRecv);
			dwTotalLen = dwDataLen;
			m_pReceiver->OnReceiveBegin(dwTotalLen, m_strIP, m_wPort);
		}
		else 
		{
			m_pReceiver->OnReceive(buf, dwRecv, m_strIP, m_wPort);
			memset(buf, 0, RECV_BUF_LEN);
			dwDataLen -= dwRecv;
			if (0 == dwDataLen)
			{
				m_pReceiver->OnReceiveEnd(dwTotalLen, m_strIP, m_wPort);
				dwRecv = 4; dwTotalLen  = 0;
				continue;
			}
		}
		if (dwDataLen < RECV_BUF_LEN)
		{
			dwRecv = dwDataLen;
		}
		else
		{
			dwRecv = RECV_BUF_LEN;
		}
	}
	TRACE("RecvThread %d is exit... \n", GetCurrentThreadId());
}

DWORD CTCPNet::CRecvThread::Clean()
{
   return 0;
}

bool CTCPNet::CRecvThreadList::AddThread(CRecvThread* pRecvThread)
{
   ASSERT(pRecvThread);
   CAutoLockEx lock(m_cs_list);
   try
   {
	   m_list_recv.push_back(pRecvThread);
   }
   catch (exception& e)
   {
	   TRACE(_T("%s exception: %s \n"), typeid(e).name(), e.what()); 
	   return false;
   }
   return true;
}

void CTCPNet::CRecvThreadList::RemoveThread(CRecvThread* pRecvThread)
{
   ASSERT(pRecvThread);
   CAutoLockEx lock(m_cs_list);
   m_list_recv.remove(pRecvThread);
}

void CTCPNet::CRecvThreadList::ClearAllThread()
{
	CAutoLockEx lock(m_cs_list);
	for (list<CRecvThread*>::iterator iter = m_list_recv.begin(); iter != m_list_recv.end(); )
	{
		delete (*iter); iter = m_list_recv.erase(iter);
	}
}
//////////////////////////////////////////////////////////////////////////
//CTCPNet实现
CTCPNet::CTCPNet():
m_thlist_send(new CSendThreadList),
m_thlist_recv(new CRecvThreadList),
m_th_listen(new CListenThread(this))
{
  
}

CTCPNet::~CTCPNet()
{
}

bool CTCPNet::Start(WORD wListenPort)
{
	return m_th_listen->Start(wListenPort); 
}

void CTCPNet::Stop()
{
    m_th_listen->Stop();
	m_thlist_send->ClearAllThread();
	m_thlist_recv->ClearAllThread();
}

void CTCPNet::CloseAllConnection()
{
	m_thlist_send->ClearAllThread();
	m_thlist_recv->ClearAllThread();
}
/*******************************************************************************
  @brief 异步发送数据
  
  @param SendBuf 发送缓冲区
  @param strIP   目标IP
  @param wPort   目标端口
  @return        true表示投递数据包成功,反之失败
*******************************************************************************/
bool CTCPNet::SendData(const CTCPNetBuf& SendBuf, const string& strIP, WORD wPort, bool bSendLen/*=true*/)
{
	 CSendTask* pSendTask = new CSendTask(SendBuf, this, bSendLen);
     CSendThread* pSendThread = m_thlist_send->GetThread(strIP, wPort);
     if (!pSendThread)
	 {
		 pSendThread = m_thlist_send->NewThread(strIP, wPort);
		 if (!pSendThread) 
		 {
			 pSendTask->Release(); return false;
		 }
	 }
	 return pSendThread->Task_Eequeue_Tail(pSendTask);
}

bool CTCPNet::Connect(SOCKET s, const string& strIP, WORD wPort, DWORD dwTimeout /*= INFINITE*/)
{
	ASSERT(s !=INVALID_SOCKET);

	sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(wPort);
	serveraddr.sin_addr.s_addr = inet_addr(strIP.c_str());

	unsigned long ul = 1;
	int ret = ioctlsocket(s, FIONBIO, (unsigned long*)&ul);  
	if( ret == SOCKET_ERROR)
	{
		return false;
	}
	if (SOCKET_ERROR == connect(s, (sockaddr*)&serveraddr, sizeof(serveraddr)))
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			return false;
		}
	}
	timeval tv, *ptv = NULL;
	if (INFINITE != dwTimeout)
	{
		tv.tv_sec = dwTimeout/1000; 
		tv.tv_usec = (dwTimeout%1000)*1000;
		ptv = &tv;
	}
	fd_set   r;  
	FD_ZERO(&r);  
	FD_SET(s,  &r);  
	ret = select(0, 0, &r, 0, ptv);  
	if( ret <=  0 )  
	{  
		if (0 == ret)  WSASetLastError(WSAETIMEDOUT);
		return false;
	}  
	//一般非锁定模式套接比较难控制，可以根据实际情况考虑   再设回阻塞模式  
	unsigned   long   ul1=   0 ;  
	ret   =   ioctlsocket(s, FIONBIO, (unsigned long*)&ul1);  
	if( ret == SOCKET_ERROR)
	{  
		return false;
	}   
	return  true;
}

int CTCPNet::SelectSend(SOCKET s, const void* pData, DWORD dwDataLen, DWORD dwTimeout/*= INFINITE*/)
{
	fd_set writefds;
	FD_ZERO(&writefds);
	FD_SET(s, &writefds);

	timeval  tv, *ptv = NULL;
	if (INFINITE != dwTimeout)
	{
		tv.tv_sec = dwTimeout/1000;
		tv.tv_usec = (dwTimeout%1000) * 1000;
		ptv = &tv;
	}

	int  ret = select(0, 0, &writefds, NULL, ptv);
	if ( ret == 0 || ret == SOCKET_ERROR)
	{
		if (0 == ret)  WSASetLastError(WSAETIMEDOUT);
		return  0;
	}

	if (FD_ISSET(s, &writefds) == 0)
	{
		return 0;
	}

	ret = send(s, (const char*)pData, dwDataLen, 0);
	if (ret == SOCKET_ERROR)
	{
		TRACE("SelectSend Failed: %d \r\n", WSAGetLastError());
		return  0;
	}
	else if (ret == 0)
	{
		return 0;
	}	

	return ret;
}

bool CTCPNet::SelectSendAll(SOCKET s, const void* pData, DWORD dwDataLen, DWORD dwTimeout/*= INFINITE*/)
{
	for (int ret, nTrans = 0; nTrans < dwDataLen; nTrans += ret)
	{
		ret = SelectSend(s, (const char*)pData + nTrans, dwDataLen - nTrans, dwTimeout);
		if (ret == 0)
		{
			return false;
		}
	}
	return true;
}

bool CTCPNet::SendData(SOCKET s, const void* pData, DWORD dwDataLen, DWORD dwTimeout, bool bSendLen /*= true*/)
{
	ASSERT(s != INVALID_SOCKET);
	if (bSendLen && !SelectSendAll(s, &dwDataLen, sizeof(dwDataLen), dwTimeout))
	{
		return false;
	}
	return SelectSendAll(s, pData, dwDataLen, dwTimeout);
}

/**
@brief 阻塞接收数据    

*/
int CTCPNet::BlockRecv(SOCKET s, void *pData, DWORD dwDataLen)
{
	ASSERT(s != INVALID_SOCKET);
	return recv(s, (char*)pData, dwDataLen, 0);
}

/**
@brief 阻塞接收全部数据     
*/
bool CTCPNet::BlockRecvAll(SOCKET s, void *pData, DWORD dwDataLen)
{
	for (int ret, nTrans = 0; nTrans < dwDataLen; nTrans += ret)
	{
		ret = BlockRecv(s, (char*)pData + nTrans, dwDataLen - nTrans);
		if (ret <= 0)
		{
			return false;
		}
	}
	return true;
}
/**
@brief 阻塞接收全部数据  

*  调用BlockRecvAll实现

*/
bool CTCPNet::RecvData(SOCKET s, void *pData, DWORD dwDataLen)
{
	return BlockRecvAll(s, pData, dwDataLen);
}

//////////////////////////////////////////////////////////////////////////
/**
*  得到通讯对方的IP和端口

@param s  套接字句柄
@param strIP  通讯对方IP地址
@param wPort  通讯对方端口
*/
//////////////////////////////////////////////////////////////////////////
void CTCPNet::GetPeerAddr(SOCKET s, string &strIP, WORD &wPort)
{
	ASSERT(s != INVALID_SOCKET);
	sockaddr_in  clientaddr;
	int dwLen = sizeof(clientaddr);
	getpeername(s, (sockaddr*)&clientaddr, &dwLen);
	strIP = inet_ntoa(clientaddr.sin_addr);
	wPort = ntohs(clientaddr.sin_port);
}

//////////////////////////////////////////////////////////////////////////
//virtual function 
//////////////////////////////////////////////////////////////////////////

/**
*  当读取每块数据时, 会自动调用该虚函数, 该虚函数可被派生类重写

@param dwTrans  指示发送方要发送的数据块大小
@param strIP    客户端IP地址
@param wPort    客户端端口
*/
//////////////////////////////////////////////////////////////////////////
void CTCPNet::OnReceiveBegin(DWORD dwTrans, const string strIP, WORD wPort)
{

}

//////////////////////////////////////////////////////////////////////////
/**
*  当读取数据完成后, 会自动调用该虚函数, 该虚函数可被派生类重写，以处理实际数据

@param pData     指向接收到的数据指针
@param dwTrans   接收到的数据实际大小, 该值是不定的
@param strIP     客户端IP地址
@param wPort     客户端端口
*/
//////////////////////////////////////////////////////////////////////////
void CTCPNet::OnReceive(const void *pData, DWORD dwTrans, const string strIP, WORD wPort)
{

}

//////////////////////////////////////////////////////////////////////////
/**
*   当读取每块数据完成后, 会自动调用该虚函数, 该虚函数可被派生类重写

@param dwTrans  接收到的数据块实际总大小, 该参数大小总等于OnReceiveEnd中的dwTrans参数
@param strIP    客户端IP地址
@param wPort    客户端端口
*/
//////////////////////////////////////////////////////////////////////////
void CTCPNet::OnReceiveEnd(DWORD dwTrans, const string strIP, WORD wPort)
{


}

/*******************************************************************************
  * 当接受数据发生错误时,回调此虚函数,该虚函数可被派生类重写
  
  @param dwError  错误代码,如果值为0表示对方关闭连接
  @param strIP    客户端IP
  @param wPort    客户端端口
*******************************************************************************/
void CTCPNet::OnReceiveError(DWORD dwError, const string strIP, WORD wPort)
{

}

/*******************************************************************************
  * 当发送数据成功完成时,回调此虚函数,该虚函数可被派生类重写
  
  @param dwTrans  发送了的字节数
  @param strIP    目标IP
  @param wPort    目标端口
*******************************************************************************/
void CTCPNet::OnSendComplete(CTCPNetBuf& SendBuf,DWORD dwTrans, const string strIP, WORD wPort)
{

}

/*******************************************************************************
  * 当发送数据发生错误时,回调此虚函数,该虚函数可被派生类重写
  
  @param dwTrans  错误代码
  @param strIP    目标IP
  @param wPort    目标端口
*******************************************************************************/
void CTCPNet::OnSendError(DWORD dwError, const string strIP, WORD wPort)
{

}
