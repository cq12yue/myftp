#ifndef _TCPNET_H
#define _TCPNET_H

#include ".\common\macro.h"
#include ".\common\autolock.h"
#include ".\common\shared_value_ptr.h"
#include ".\thread\threadbase.h"
#include "netcomm.h"
#include <list>
#include <deque>
#include <algorithm>
#include <functional>
using namespace std;

namespace netcomm
{
	/*******************************************************************************
      @brief 基于TCP协议的通讯类
      
      * 既可作服务器也可作客户端
	  * 多线程实现异步发送数据
	  * 多线程接收客户端数据
    *******************************************************************************/
	class CTCPNet 
	{
		class CListenThread;
		class CSendThread;
		class CRecvThread;
		class CSendThreadList;
		class CRecvThreadList;
	public:
		class CTCPNetBuf;       ///< 发送缓冲区共享类
	public:
        CTCPNet();
		virtual ~CTCPNet();

	public:
		///< 关闭所有连接
		void  CloseAllConnection();

        bool Start(WORD wListenPort) ;
        void Stop();
		bool SendData(const CTCPNetBuf& SendBuf, const string& strIP, WORD wPort, bool bSendLen = true);
		bool SendData(const void* pData, DWORD dwDataLen, const string& strIP, WORD wPort, DWORD dwTimeout, bool bSendLen = true);

	//////////////////////////////////////////////////////////////////////////
	//事件回调函数
	protected:
		virtual void OnSendComplete(CTCPNetBuf& SendBuf,DWORD dwTrans, const string strIP, WORD wPort);
		virtual void OnSendError(DWORD dwError, const string strIP, WORD wPort);
		virtual void OnReceiveBegin(DWORD dwTrans, const string strIP, WORD wPort);
		virtual void OnReceive(const void* pData, DWORD dwTrans, const string strIP, WORD wPort);
		virtual void OnReceiveEnd(DWORD dwTrans, const string strIP, WORD wPort);
        virtual void OnReceiveError(DWORD dwError, const string strIP, WORD wPort);
		
	protected:
		static bool SendData(SOCKET s,  const void* pData, DWORD dwDataLen, DWORD dwTimeout = INFINITE, bool bSendLen = true);
		static bool Connect(SOCKET s, const string& strIP, WORD wPort, DWORD dwTimeout = INFINITE);
		static int  SelectSend(SOCKET s, const void* pData, DWORD dwDataLen, DWORD dwTimeout = INFINITE);
		static bool SelectSendAll(SOCKET s, const void* pData, DWORD dwDataLen, DWORD dwTimeout = INFINITE);
		
		static bool RecvData(SOCKET s, void* pData, DWORD dwDataLen);
		static int  BlockRecv(SOCKET s, void* pData, DWORD dwDataLen);
		static bool BlockRecvAll(SOCKET s, void* pData, DWORD dwDataLen);

		static void GetPeerAddr(SOCKET s, string& strIP, WORD& nPort);

	private:
     	auto_ptr<CListenThread>    m_th_listen; 
		auto_ptr<CSendThreadList>  m_thlist_send;
		auto_ptr<CRecvThreadList>  m_thlist_recv;

	private:
		class CSendTask;        ///< 发送任务类 
	};
    /*******************************************************************************
      @brief 发送缓冲区共享类

      * 基于引用计数机制的智能指针实现对堆上的内存管理
	    1. 当外界不再使用时,自动释放
		2. 对引用计数作了同步,是线程安全的
    *******************************************************************************/
	class CTCPNet::CTCPNetBuf : public shared_ptr<BYTE>
	{
	public:
		explicit CTCPNetBuf(BYTE* p, unsigned long ulLen):
		shared_ptr<BYTE>(p),m_ulLen(ulLen)
		{ }
		virtual ~CTCPNetBuf() 
		{ }
		inline unsigned long getlen() const
		{
			return m_ulLen;
		}
	private:
		unsigned long m_ulLen;
	};

	///< 发送任务包类
	class CTCPNet::CSendTask
	{
		friend class CSendThread;
	public:
		CSendTask(const CTCPNetBuf& tcpnetBuf, CTCPNet* pSender, bool bSendLen = true);
		void Release() { delete this; }
	private:
		~CSendTask();

	private:
		CTCPNetBuf   m_tcpnetBuf;
		CTCPNet*     m_pSender;
		bool         m_bSendLen;
	};
	
	///< 监听线程类
	class CTCPNet::CListenThread : public CThreadBase
	{
	public:
		CListenThread(CTCPNet* pListener);
		~CListenThread();

		bool Start(WORD wPort);
        void Stop();
	protected:
		virtual void Signal();
		virtual bool Init();
		virtual void Run();
		virtual DWORD Clean();
	private:
		CTCPNet* m_pListener;
		SOCKET m_sListen;
		WORD   m_wPort;
	};
    ///< 发送线程类
	class CTCPNet::CSendThread : public CThreadBase
	{
		friend class CTCPNet;
		friend class CSendThreadList;
		enum { Delay = 2500 };

	public:
		CSendThread(const string& strIP, WORD wPort);
		~CSendThread();
		bool Task_Eequeue_Tail(CSendTask* pSendTask);
		CSendTask* Task_Dequeue_Head();
		bool Task_Is_Empty();
		DWORD WaitTask(DWORD dwTimeout  = INFINITE);
        void SendTask(CSendTask* pSendTask);

	protected:
		virtual void Signal();
		virtual bool Init();
		virtual void Run();
		virtual DWORD Clean();
	private:
		string     m_strIP;
		WORD	   m_wPort;
		deque<CSendTask*> m_deq_send; 
		CCriticalSectionEx  m_cs_task;
		HANDLE     m_h_task_not_empty;
		HANDLE     m_hExit;
		SOCKET     m_sSend;
		bool       m_bConnected;
	};
	///< 接收线程类
	class CTCPNet::CRecvThread : public CThreadBase
	{
		enum { RECV_BUF_LEN = 4096 };
		friend class CTCPNet;
		friend class CRecvThreadList;
	public:
		CRecvThread(CTCPNet* pReceiver, SOCKET s, const string& strIP, WORD wPort);
		~CRecvThread();
	protected:
		virtual void Signal();
		virtual bool Init();
		virtual void Run();
		virtual DWORD Clean();
	private:
		CTCPNet* m_pReceiver;
		SOCKET   m_sRecv;
		string   m_strIP;
        WORD     m_wPort;
	};
    ///< 发送线程列表类
	class CTCPNet::CSendThreadList
	{
	public:
        CSendThread* GetThread(const string& strIP, WORD wPort);
		CSendThread* NewThread(const string& strIP, WORD wPort);
		void DeleteThread(CSendThread* pSendThread);
		void ClearAllThread();
	private:
		list<CSendThread*> m_list_send;
		CCriticalSectionEx m_cs_list;
	};
	///< 接收线程列表类
	class CTCPNet::CRecvThreadList
	{
	public:
        bool AddThread(CRecvThread* pRecvThread);
		void RemoveThread(CRecvThread* pRecvThread);
		void ClearAllThread();
	private:
		list<CRecvThread*> m_list_recv;
		CCriticalSectionEx m_cs_list;
	};
}


#endif
