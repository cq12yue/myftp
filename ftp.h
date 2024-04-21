#ifndef _FTP_H
#define _FTP_H

#include "netcomm.h"
#include "tcpnet.h"
#include "zthunk.h"
#include <map>
using namespace std;

namespace netcomm
{
	class CFtp : public CTCPNet
	{
		enum TransmitFlag { File, Dir };
		enum NotifyState  { TRANS_BEGIN, TRANS_END, TRANS_BREAK }; 
		enum              { SendBufLen=4096, SendDelay=4000, RecvDelay=4000, ConnectDelay=4000, Elapse=500 };
		
		struct RECV_FILE_MSG;   ///< 接收方文件信息
		struct TRANSMIT_MSG;    ///< 传输结构体
	    struct THREAD_MSG;      ///< 线程消息结构
        struct TASK_INFO;       ///< 任务信息结构

	public:
		// 定义通知窗口UI消息,使用RegisterWidnowMessage注册来保证唯一性
        static UINT WM_TRANS_BEGIN ;   ///< 准备好传送
        static UINT WM_TRANS_ING;      ///< 传送进行中
		static UINT WM_TRANS_END;      ///< 传送结束,当成功或出现错误时会结束
        static UINT WM_TRANS_BREAK;    ///< 传送中断,传输过程中因对方问题引起中断,发送此消息 
		static UINT WM_TRANS_REQUEST;  ///< 传输请求消息
		static UINT WM_RECV_ERROR ;    ///< 接收数据错误通知消息

		///< 定义错误代码标志
		enum 
		{ 
			errorNo        =  0L,    ///< 操作成功
            errorOpenFail     ,      ///< 打开文件失败
			errorReadFail     ,      ///< 读文件失败
			errorCalcingFileSize ,   ///< 正在计算文件大小
			errorCalcingDirSize,     ///< 正在计算目录大小
			errorConnectIng   ,      ///< 正在连接
			errorConnectFail  ,      ///< 网络不通,连接失败
			errorConnectOK    ,      ///< 连接成功
            errorConnectBreak ,      ///< 连接中断
            errorUserCancel   ,      ///< 用户取消传送
			errorSrcEmpty     ,      ///< 源路径为空
            errorDestEmpty    ,      ///< 目标路径为空
            errorInvalidHwnd  ,      ///< 通知窗口无效
			errorSrcInvalid   ,      ///< 源目录或文件不存在
			errorDestInvalid  ,      ///< 目标路径无效
            errorTransIng     ,      ///< 任务正在传输
			errorMemoryLack   ,      ///< 内存不足
			errorWriteFail    ,      ///< 磁盘空间不够写文件失败
			errorUserJump     ,      ///< 用过跳过文件
			errorRefuseFileRequest,  ///< 对方拒绝接收文件
			errorRefuseDirRequest,   ///< 对方拒绝接收目录
			errorNum
		};

		///< 文件传输控制命令
		enum CONTROL_MSG  
		{
			// 由客户方发送的命令
			RequestOneFile   = errorNum + 1, ///< 客户请求发送文件
			TransFileRequest,			  ///< 发送文件请求	
			TransDirRequest,              ///< 发送目录请求
			CancelTransRequest,           ///< 取消发送请求
			SendFile      ,               ///< 正在传送文件
			CoverOneFile  ,               ///< 覆盖单个已存在文件
			CoverAllFile  ,               ///< 覆盖全部已存在文件 
			JumpOneFile   ,               ///< 跳过单个已存在文件 
			JumpAllFile   ,               ///< 跳过全部存在文件
			CancelFile    ,               ///< 取消发送
			//  由服务方返回的响应命令
			FileRequestBeRefuse,          ///< 对方拒绝接收文件
			DirRequestBeRefuse,           ///< 对方拒绝接收目录
			FileRequestBeAccept,          ///< 对方同意接收文件
			DirRequestBeAccept,			  ///< 对方同意接收目录	
			FileIsAccept  ,               ///< 文件被接收
			FileIsExist   ,               ///< 文件已存在
			FileIsUsed    ,               ///< 文件被使用
			FileWriteFail ,               ///< 文件写失败
			FileInvalidPath,              ///< 文件路径无效,即创建文件所在目录失败  
			controlNum
		};

		struct TRANSMIT_TASK;         ///< 传送任务描述
        struct TRANSMIT_INFO;         ///< 传送任务信息
        struct TRANS_FILE_MSG;        ///< 文件消息结构
		struct CLIENT_MSG ;           ///< 客户端消息结构

	public:
		CFtp();
		~CFtp();

	public:
		static bool DeleteDirectory(LPCTSTR lpPathName);

	public:
        //接收方调用
		DWORD GetTransRequestInfo(const string& strIP, WORD wPort,CString& strName, ULONGLONG& ullTotalLen);
		void  RemoveClientRequestMsg(const string& strIP, WORD wPort);
		void  AcceptTransRequest(const string& strIP, WORD wPort, LPCTSTR lpSavePath, bool bDir);
		void  RefuseTransRequest(const string& strIP, WORD wPort, bool bDir);

		void  SetTransRequestNotifyHwnd(HWND hWnd);
		void  SetRecvErrorHwnd(HWND hWnd);

		//发送方调用
		DWORD TransmitTask(const TRANSMIT_TASK& TransmitTask, HWND hWnd);
	    void  RemoveTransmitTask(const TRANSMIT_TASK TransmitTask);
		void  CancelTransmit(const TRANSMIT_TASK& TransmitTask);

	protected:
		DWORD TransmitFile(TRANSMIT_MSG* pMSG);
		DWORD TransmitNetFile(TRANSMIT_MSG* pMSG);
        DWORD TransmitLocalFile(TRANSMIT_MSG* pMSG);

		DWORD TransmitDir(TRANSMIT_MSG* pMSG);
		void  TransmitNotify(NotifyState  State, THREAD_MSG* T_MSG, const TRANSMIT_TASK* T_TASK, TRANSMIT_INFO* T_INFO);
		void  StartTimer(THREAD_MSG* T_MSG, UINT uElapse);
		void  StopTimer(THREAD_MSG* T_MSG);
        DWORD WaitForDestPrcoessTransRequest(TRANSMIT_MSG* TM_MSG,const TRANSMIT_TASK* T_TASK, TRANSMIT_INFO* T_INFO,THREAD_MSG* TH_MSG);

	private:
		void AddTransmitTaskThreadMsg(const TRANSMIT_TASK& TransmitTask, const THREAD_MSG& T_MSG);
		void AddTransmitTaskTransInfo(const TRANSMIT_TASK& TransmitTask, TASK_INFO* T_INFO);
	    void AddTaskThread(HANDLE hThread);
		bool FindTransmitTaskInfo(const TRANSMIT_TASK& TransmitTask, const TRANSMIT_TASK* &T_TASK, TASK_INFO* &T_INFO);
        THREAD_MSG*  FindTaskInfoMatchHwnd(HWND hWnd, const TRANSMIT_TASK* &T_TASK, TASK_INFO* &T_INFO);
		THREAD_MSG*  FindThreadMsg(const TRANSMIT_TASK& TransmitTask);
        void RemoveClientFile(const string& strIP, WORD wPort);
		RECV_FILE_MSG* FindFileInfoOfClient(const string& strIP, WORD wPort);
		void  AddOrUpdateClientRequestMsg(const string& strIP, WORD wPort, const TRANS_FILE_MSG& TF_REQUEST_MSG);
        TRANS_FILE_MSG* FindClientRequestMsg(const string& strIP, WORD wPort);
        
	private:
		static DWORD WINAPI TransmitThread(LPVOID lParam);
        void   TimerProc(HWND hWnd, UINT uMsg, UINT idEvent, DWORD dwTime);

    ///////////////////////////////////////////////////////////////////////////////
    //overwrite virtual funtion
	protected:
		void Cleanup();
		
		void OnReceiveBegin(DWORD dwTrans, const string strIP, WORD wPort);
		void OnReceive(const void* pData, DWORD dwTrans, const string strIP, WORD wPort);
		void OnReceiveEnd(DWORD dwTrans, const string strIP, WORD wPort);
		void OnReceiveError(DWORD dwError, const string strIP, WORD wPort);

		void OnSendComplete(CTCPNetBuf& SendBuf,DWORD dwTrans, const string strIP, WORD wPort);
        void OnSendError(DWORD dwError, const string strIP, WORD wPort);

	private:
		map<CLIENT_MSG, RECV_FILE_MSG>     m_map_client_file;
		map<CLIENT_MSG, TRANS_FILE_MSG>    m_map_client_request;
		map<TRANSMIT_TASK, THREAD_MSG>     m_map_task_threadmsg;
        map<TRANSMIT_TASK, TASK_INFO*>     m_map_task_transinfo;
//		list<HANDLE>                       m_list_thread;  

	    CCriticalSectionEx                 m_cs_task_transinfo;
		CCriticalSectionEx                 m_cs_task_thread;
//		CCriticalSectionEx                 m_cs_list_thread;
        CCriticalSectionEx                 m_cs_client_file;
        CCriticalSectionEx                 m_cs_client_request;

		ZThunk                             m_thunk;       
		HWND                               m_hTransRequestNotifyWnd;
        HWND                               m_hRecvErrorWnd;
	};

#pragma  pack(1)
	struct CFtp::TRANS_FILE_MSG
	{
        CONTROL_MSG    ctrl_msg;           //控制消息
	    struct DATA_MSG                    //数据消息
		{
			ULONGLONG ullLen; 
			FILETIME  LastWriteTime;
			BYTE      buf[SendBufLen];
		} data_msg; 
		TRANS_FILE_MSG() { memset(this, 0, sizeof(TRANS_FILE_MSG));}
	};
#pragma pack()

	struct CFtp::THREAD_MSG
	{
		HANDLE        hThread;   //线程句柄
		DWORD         dwThreadID; 
		HWND          hWnd;      //线程关联窗口句柄,用于通知消息
		UINT          uTimerID;  //hWnd窗口定时器ID  
		DWORD         dwTime;    //传送总时间
		long          lUserSet;  //用户设置
		volatile BOOL bExit;
		THREAD_MSG(HANDLE _hThread, DWORD _dwThreadID, HWND _hWnd) : 
		hThread(_hThread), hWnd(_hWnd), uTimerID(1), dwTime(0), 
		lUserSet(RequestOneFile), bExit(FALSE),	dwThreadID(_dwThreadID)
		{
			if (NULL == hWnd) lUserSet = CoverAllFile;
		}
	};

	struct CFtp::TRANSMIT_TASK
	{
		CString       strSrc;
		CString       strDest;
		string        strIP;
		WORD          wPort; 
        bool    bDelete;

		TRANSMIT_TASK(){};
		TRANSMIT_TASK(const CString& _strSrc, const CString& _strDest, bool _bDelete = false);
		TRANSMIT_TASK(const CString& _strSrc,const string& _strIP, WORD _wPort, bool _bDelete = false); 
		TRANSMIT_TASK(const TRANSMIT_TASK& other);	
		TRANSMIT_TASK& operator=(const TRANSMIT_TASK& other);
		bool operator < (const TRANSMIT_TASK& other) const;
		/**
		  @brief 判断是网络还是本机传输
		  @return 返回true表示本机传输,false表示网络传输
		*/
		inline bool IsLocalTask() const { return 0 == strIP.compare("127.0.0.1"); }
	};

	struct CFtp::TRANSMIT_INFO
	{
	public:
		friend struct TASK_INFO;

		TRANSMIT_INFO(TASK_INFO* _pTaskInfo);
		void      GetTransLen(ULONGLONG &_ullTransFileLen, ULONGLONG &_ullTransDirLen);
		void      GetTransFile(CString& strSrc, CString& strDest);
		ULONGLONG GetTotalLen();
        ULONGLONG GetTransSpeed();

	public:
		DWORD      dwError;                   ///< 传送状态 

	private:	
		volatile ULONGLONG  ullTotalDirLen;   ///< 目录或文件总长度
		volatile ULONGLONG  ullTransDirLen;   ///< 目录或文件已传输长度
		volatile ULONGLONG  ullTotalFileLen;  ///< 当前文件总长度
		volatile ULONGLONG  ullTransFileLen;  ///< 当前文件已传输长度
		volatile ULONGLONG  ullTransSpeed;    ///< 传输速度
		CString             strCurSrcFile;    ///< 当前源文件
		CString             strCurDestFile;   ///< 当前目标文件

		TASK_INFO*  pTaskInfo;
	};

	struct CFtp::TASK_INFO
	{
	public:
		TASK_INFO():TM_INFO(this) { }
		TRANSMIT_INFO  TM_INFO; 

		void SetFileLen(ULONGLONG _ullTotalFileLen, ULONGLONG _ullTransFileLen);
		void AddTransLen(ULONGLONG ullTrans);
		void SetTransFile(const CString& strSrc, const CString& strDest);
		void SetTotalLen(ULONGLONG ullTotalLen);
        void ComputeTransSpeed(DWORD dwTotalTime);

	private:
		CCriticalSectionEx csTransLen;
		CCriticalSectionEx csStrFile;
		CCriticalSectionEx csTransSpeed;
		friend struct TRANSMIT_INFO;
	};

	struct CFtp::TRANSMIT_MSG
	{
		TransmitFlag   enFlag;         //传输标志,是文件还是目录 
		TRANSMIT_TASK  TransmitTask;   //当前任务 
		TRANSMIT_TASK* RootTask;       //根任务
		CFtp*          pFtp; 

		TRANSMIT_MSG(TransmitFlag, const TRANSMIT_TASK&, CFtp* );
		TRANSMIT_MSG(const TRANSMIT_MSG& other);
	};
	
	struct CFtp::CLIENT_MSG 
	{
		string strIP;
		WORD   wPort;

		CLIENT_MSG(const string& _strIP, WORD _wPort);
		bool operator< (const CLIENT_MSG& other) const;
	};

	struct CFtp::RECV_FILE_MSG
	{
		enum { BUF_LEN = sizeof(TRANS_FILE_MSG) };
		
		HANDLE    hFile;         //文件句柄 
		DWORD     dwRecv;        //已接收缓冲区长度
		ULONGLONG ullTotalLen;   //文件总长度
		ULONGLONG ullRecvLen;    //已接收长度
		BYTE      buf[BUF_LEN];  //接收缓冲区

		RECV_FILE_MSG() ;
	};
}


#endif
