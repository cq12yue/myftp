#include "stdafx.h"
#include "ftp.h"
#include <shlobj.h>
using namespace netcomm;

/********************************************************************************
   2009-7-1   修正了退出程序时重复关闭线程句柄引起的BUG, 不论发送方是否需要UI控制
              都能正确地关闭线程句柄
   2009-7-10  扩展优化了本地传输
   2009-8-7   同步了目标文件修改时间和本地相同文件的修改时间
              当跳过文件时同步了已传输长度,使得UI能够正确显示进度信息
			  接收方以共享读写方式打开文件再写数据
   2009-8-11  支持用户界面重连功能
********************************************************************************/

UINT CFtp::WM_TRANS_BEGIN = RegisterWindowMessage(_T("TRANS_BEGIN"));
UINT CFtp::WM_TRANS_ING   = RegisterWindowMessage(_T("TRANS_ING"));
UINT CFtp::WM_TRANS_END   = RegisterWindowMessage(_T("TRANS_END"));
UINT CFtp::WM_TRANS_BREAK = RegisterWindowMessage(_T("TRANS_BREAK"));
UINT CFtp::WM_TRANS_REQUEST = RegisterWindowMessage(_T("TRANS_REQUEST"));
UINT CFtp::WM_RECV_ERROR    = RegisterWindowMessage(_T("RECV_ERROR"));

//////////////////////////////////////////////////////////////////////////////////////////
//嵌套结构体实现
/**
  @brief 本机传输构造函数    

  * 只需显式设置strSrc,strDest成员变量                                                                 
*/
CFtp::TRANSMIT_TASK::TRANSMIT_TASK(const CString& _strSrc, const CString& _strDest, bool _bDelete /*false*/):
strSrc(_strSrc),strDest(_strDest),strIP("127.0.0.1"),wPort(0),
bDelete(_bDelete)
{
	strSrc.Trim(); 
	strDest.Trim();
}

/**
   @brief 网络传输构造函数    
   
   * 除strDest不用设置外,其它成员变量显示指定,strDest由对方决定
*/
CFtp::TRANSMIT_TASK::TRANSMIT_TASK(const CString& _strSrc, const string& _strIP, WORD _wPort,bool _bDelete /*false*/):
strSrc(_strSrc),strIP(_strIP),wPort(_wPort),
bDelete(_bDelete)
{
	strSrc.Trim();  
	Trim(strIP);
}

/**
  @brief  拷贝构造函数   
*/
CFtp::TRANSMIT_TASK::TRANSMIT_TASK(const TRANSMIT_TASK& other):
strSrc(other.strSrc),strDest(other.strDest),strIP(other.strIP),
wPort(other.wPort),bDelete(other.bDelete)
{
}

/**
  @brief  重写赋值运算符   
*/
CFtp::TRANSMIT_TASK& CFtp::TRANSMIT_TASK::operator =(const TRANSMIT_TASK& other)
{
	if (this != &other)
	{
	  strSrc  = other.strSrc;
	  strDest = other.strDest;
	  strIP   = other.strIP;
	  wPort   = other.wPort;
	  bDelete = other.bDelete;
	}
	return *this;
}

/**
  @brief  排序比较函数   
*/
bool CFtp::TRANSMIT_TASK::operator < (const TRANSMIT_TASK& other) const
{
	int nRet = (strSrc == other.strSrc);
	if (0 == nRet)
	{
		if (IsLocalTask())
		{
            nRet = (strDest == other.strDest);
		}
		else
		{
			nRet = (strIP == other.strIP);
			if (0 == nRet)
				return wPort < other.wPort;
		}
	}
	return nRet < 0;
}
//////////////////////////////////////////////////////////////////////////////////////////
//TASK_INFO
inline void CFtp::TASK_INFO::SetFileLen(ULONGLONG _ullTotalFileLen, ULONGLONG _ullTransFileLen)
{
	CAutoLockEx AutoLock(csTransLen);
	TM_INFO.ullTotalFileLen = _ullTotalFileLen;
	TM_INFO.ullTransFileLen = _ullTransFileLen;
}

inline void CFtp::TASK_INFO::AddTransLen(ULONGLONG ullTrans)
{
	CAutoLockEx AutoLock(csTransLen);
	TM_INFO.ullTransFileLen += ullTrans;
	TM_INFO.ullTransDirLen  += ullTrans;
}

inline void CFtp::TASK_INFO::SetTransFile(const CString& strSrc, const CString& strDest)
{
	CAutoLockEx  AutoLock(csStrFile);
	TM_INFO.strCurSrcFile  = strSrc;
	TM_INFO.strCurDestFile = strDest;
}
inline void CFtp::TASK_INFO::SetTotalLen(ULONGLONG ullTotalLen)
{
	CAutoLockEx Autolock(csTransLen);
	TM_INFO.ullTotalDirLen = ullTotalLen;
}

inline void CFtp::TASK_INFO::ComputeTransSpeed(DWORD dwTotalTime)
{
	CAutoLockEx  AutoLock(csTransSpeed);
	TM_INFO.ullTransSpeed = 1000 * TM_INFO.ullTransDirLen/dwTotalTime;
}

CFtp::TRANSMIT_INFO::TRANSMIT_INFO(TASK_INFO* _pTaskInfo):
pTaskInfo(_pTaskInfo),
ullTotalDirLen(0), ullTotalFileLen(0), ullTransDirLen(0),ullTransFileLen(0),
dwError(errorNo)
{

}

/*inline*/ void CFtp::TRANSMIT_INFO::GetTransLen(ULONGLONG &_ullTransFileLen, ULONGLONG &_ullTransDirLen)
{
	CAutoLockEx AutoLock(pTaskInfo->csTransLen);
	_ullTransDirLen  = ullTransDirLen;
	_ullTransFileLen = ullTransFileLen;
}

/*inline*/ void CFtp::TRANSMIT_INFO::GetTransFile(CString& strSrc, CString& strDest)
{
	CAutoLockEx  AutoLock(pTaskInfo->csStrFile);
	strSrc  = strCurSrcFile;
	strDest = strCurDestFile;
}

ULONGLONG CFtp::TRANSMIT_INFO::GetTotalLen()
{
	CAutoLockEx Autolock(pTaskInfo->csTransLen);
	return ullTotalDirLen;
}

ULONGLONG CFtp::TRANSMIT_INFO::GetTransSpeed()
{
    CAutoLockEx AutoLock(pTaskInfo->csTransSpeed);
	return ullTransSpeed;
}

 CFtp::TRANSMIT_MSG::TRANSMIT_MSG(TransmitFlag _enFlag, const TRANSMIT_TASK& _TransmitTask, CFtp* _pFtp):
TransmitTask(_TransmitTask), RootTask(&TransmitTask), enFlag(_enFlag),pFtp(_pFtp)
{
}

CFtp::TRANSMIT_MSG::TRANSMIT_MSG(const TRANSMIT_MSG& other):
TransmitTask(other.TransmitTask),RootTask(other.RootTask),enFlag(other.enFlag),pFtp(other.pFtp)
{
}

CFtp::CLIENT_MSG::CLIENT_MSG(const string& _strIP, WORD _wPort):
strIP(_strIP), wPort(_wPort)
{
}

bool CFtp::CLIENT_MSG::operator< (const CLIENT_MSG& other) const
{
	int nRet = strIP.compare(other.strIP) ;
	if (nRet == 0)
	{
		return wPort < other.wPort;
	}
	return nRet < 0;
}

CFtp::RECV_FILE_MSG::RECV_FILE_MSG() 
{
	memset(this, 0, sizeof(RECV_FILE_MSG)); 
	hFile = INVALID_HANDLE_VALUE;
}

//////////////////////////////////////////////////////////////////////////////////////////
//CFtp实现
inline CFtp::RECV_FILE_MSG* CFtp::FindFileInfoOfClient(const string& strIP, WORD wPort)
{
	CAutoLockEx  AutoLock(m_cs_client_file);
	map<CLIENT_MSG, RECV_FILE_MSG>::iterator iter = m_map_client_file.find(CLIENT_MSG(strIP, wPort));
	if (iter != m_map_client_file.end())
		return &(iter->second);
	return NULL;
}

inline void CFtp::RemoveClientFile(const string& strIP, WORD wPort)
{
	CAutoLockEx  AutoLock(m_cs_client_file);
	map<CLIENT_MSG, RECV_FILE_MSG>::iterator iter = m_map_client_file.find(CLIENT_MSG(strIP, wPort));
	if (iter != m_map_client_file.end())
	{
		CloseHandle(iter->second.hFile);  
		iter->second.hFile = INVALID_HANDLE_VALUE;
		m_map_client_file.erase(iter);
	}
}

inline CFtp::THREAD_MSG*  CFtp::FindThreadMsg(const TRANSMIT_TASK& TransmitTask) 
{
	CAutoLockEx  AutoLock(m_cs_task_thread);
	map<TRANSMIT_TASK, THREAD_MSG>::iterator iter = m_map_task_threadmsg.find(TransmitTask);
	if (iter != m_map_task_threadmsg.end())
	{
		return &(iter->second);
	}
	return NULL;
}

inline bool CFtp::FindTransmitTaskInfo(const TRANSMIT_TASK& TransmitTask, const TRANSMIT_TASK* &T_TASK, TASK_INFO* &T_INFO)
{
	CAutoLockEx  AutoLock(m_cs_task_transinfo);
	map<TRANSMIT_TASK, TASK_INFO*>::iterator iter = m_map_task_transinfo.find(TransmitTask);
	if (iter != m_map_task_transinfo.end())
	{
		 T_TASK = &(iter->first);	T_INFO = (iter->second);
		 return true;
	}
	T_TASK = NULL;  T_INFO = NULL;
	return false;
}

void CFtp::RemoveTransmitTask(const TRANSMIT_TASK TransmitTask)
{
	{
		CAutoLockEx  AutoLock(m_cs_task_transinfo);
		map<TRANSMIT_TASK, TASK_INFO*>::iterator iter = m_map_task_transinfo.find(TransmitTask);
		if (iter != m_map_task_transinfo.end())
		{  
			delete iter->second;   m_map_task_transinfo.erase(iter);
		}
	}
	{
		CAutoLockEx  AutoLock(m_cs_task_thread);
		map<TRANSMIT_TASK, THREAD_MSG>::iterator iter = m_map_task_threadmsg.find(TransmitTask);
		if (iter != m_map_task_threadmsg.end())
		{
			if (iter->second.hWnd)
			{
				KillTimer(iter->second.hWnd, iter->second.uTimerID);
			}
            m_map_task_threadmsg.erase(iter);
		}
	}
}

inline void CFtp::AddTransmitTaskThreadMsg(const TRANSMIT_TASK& TransmitTask, const THREAD_MSG& T_MSG)
{
    CAutoLockEx AutoLock(m_cs_task_thread);
	m_map_task_threadmsg.insert(make_pair(TransmitTask, T_MSG));
}

inline void CFtp::AddTransmitTaskTransInfo(const TRANSMIT_TASK& TransmitTask, TASK_INFO* T_INFO)
{
    CAutoLockEx AutoLock(m_cs_task_transinfo);
	m_map_task_transinfo.insert(make_pair(TransmitTask, T_INFO));
}

inline void CFtp::AddTaskThread(HANDLE hThread)
{
//    CAutoLockEx AutoLock(m_cs_list_thread);
//	m_list_thread.push_back(hThread);
}

inline CFtp::THREAD_MSG* CFtp::FindTaskInfoMatchHwnd(HWND hWnd, const TRANSMIT_TASK *&T_TASK, TASK_INFO *&T_INFO)
{
	typedef map<TRANSMIT_TASK,THREAD_MSG>::value_type map_value_type;
	typedef map<TRANSMIT_TASK,THREAD_MSG>::iterator   map_iter;
	struct  FindTaskInfo : public unary_function<map_value_type,bool>
	{
	   FindTaskInfo(HWND hWnd):m_hWnd(hWnd) { }

	   bool operator() (const map_value_type& value_type)
	   {
            return value_type.second.hWnd == m_hWnd;
	   }
	private:
		HWND m_hWnd;
	};

	FindTaskInfo   match(hWnd);
    CAutoLockEx AutoLock(m_cs_task_thread);
    map_iter iter = find_if(m_map_task_threadmsg.begin(), m_map_task_threadmsg.end(), match);
    if (iter != m_map_task_threadmsg.end())
	{
		FindTransmitTaskInfo(iter->first, T_TASK, T_INFO);
		return &(iter->second);
	}
	T_TASK =  NULL; T_INFO = NULL;
	return NULL;
}

//////////////////////////////////////////////////////////////////////////////////////////
CFtp::CFtp():
m_hRecvErrorWnd(NULL),
m_hTransRequestNotifyWnd(NULL)
{
	SetShareLink(false);
}

CFtp::~CFtp()
{
	Cleanup();
	//这个最后释放
	map<TRANSMIT_TASK,TASK_INFO*>::iterator iter;
	CAutoLockEx AutoLock(m_cs_task_transinfo);
	for (iter = m_map_task_transinfo.begin(); iter != m_map_task_transinfo.end(); )
	{
		delete iter->second;  iter = m_map_task_transinfo.erase(iter);
	}
}

void CFtp::Cleanup()
{
	{
		map<TRANSMIT_TASK, THREAD_MSG>::iterator iter;
		THREAD_MSG *T_MSG;
		CAutoLockEx  AutoLock(m_cs_task_thread);
		for (iter = m_map_task_threadmsg.begin(); iter != m_map_task_threadmsg.end(); )
		{ 
		     T_MSG = &iter->second;	 
			 InterlockedExchange((volatile long*)&T_MSG->bExit, TRUE);
			 if (T_MSG->hThread)
			 {
				 WaitForSingleObject(T_MSG->hThread, INFINITE);
				 CloseHandle(T_MSG->hThread);
			 }
			 iter = m_map_task_threadmsg.erase(iter);
		}
	}
	{
		map<CLIENT_MSG, TRANS_FILE_MSG>::iterator iter;
		CAutoLockEx  AutoLock(m_cs_client_request);
		for (iter = m_map_client_request.begin(); iter != m_map_client_request.end(); )
		{
			iter = m_map_client_request.erase(iter);
		}

	}
}

/**
  @brief  删除目录或文件    
  @param lpPathName  目录或文件路径名
  @return  true表示删除成功,false表示失败

  * 该操作直接删除目录或文件,而不是放进回收站
*/
bool CFtp::DeleteDirectory(LPCTSTR lpPathName)
{
	SHFILEOPSTRUCT FileOp; 
	ZeroMemory((void*)&FileOp,sizeof(SHFILEOPSTRUCT));

	FileOp.fFlags = FOF_NOCONFIRMATION; 
	FileOp.hNameMappings = NULL; 
	FileOp.hwnd = NULL; 
	FileOp.lpszProgressTitle = NULL; 
	FileOp.pFrom = lpPathName; 
	FileOp.pTo = NULL; 
	FileOp.wFunc = FO_DELETE; 

	return SHFileOperation(&FileOp) == 0;
}

//////////////////////////////////////////////////////////////////////////
//接收方调用
/*******************************************************************************
  @brief  设置传输请求通知窗口
  
  @param hWnd  通知窗口句柄
*******************************************************************************/
void CFtp::SetTransRequestNotifyHwnd(HWND hWnd)
{
	ASSERT(hWnd && IsWindow(hWnd));
	m_hTransRequestNotifyWnd = hWnd;
}

/*******************************************************************************
  @brief 设置错误通知窗口
  
  *
*******************************************************************************/
void CFtp::SetRecvErrorHwnd(HWND hWnd)
{
	ASSERT(hWnd && IsWindow(hWnd));
	m_hRecvErrorWnd = hWnd;
}

/*******************************************************************************
  @brief  查找客户传输请求消息
  
  @param strIP   发送方IP  
  @param wPort   发送方端口
  @return  若查找成功则返回对应结构指针, 否则返回NULL 
*******************************************************************************/
CFtp::TRANS_FILE_MSG* CFtp::FindClientRequestMsg(const string& strIP, WORD wPort)
{
	CAutoLockEx  AutoLock(m_cs_client_request);
	map<CLIENT_MSG, TRANS_FILE_MSG>::iterator iter = m_map_client_request.find(CLIENT_MSG(strIP, wPort));
	if (iter != m_map_client_request.end())
	{
		return &iter->second;
	}
	return NULL;
}

/*******************************************************************************
  @brief  移除客户传输请求消息
  
  @param strIP   发送方IP  
  @param wPort   发送方端口
*******************************************************************************/
void CFtp::RemoveClientRequestMsg(const string& strIP, WORD wPort)
{
	CAutoLockEx  AutoLock(m_cs_client_request);
    m_map_client_request.erase(CLIENT_MSG(strIP, wPort));
}

/*******************************************************************************
  @brief  增加客户传输请求消息
  
  @param strIP           发送方IP  
  @param wPort           发送方端口
  @param TF_REQUEST_MSG  请求消息结构
*******************************************************************************/
void CFtp::AddOrUpdateClientRequestMsg(const string& strIP, WORD wPort, const TRANS_FILE_MSG& TF_REQUEST_MSG)
{
	CLIENT_MSG C_MSG(strIP, wPort);

	CAutoLockEx  AutoLock(m_cs_client_request);
	map<CLIENT_MSG,TRANS_FILE_MSG>::iterator iter = m_map_client_request.lower_bound(C_MSG);
	if (iter != m_map_client_request.end() && !(m_map_client_request.key_comp()(C_MSG,iter->first)))
	{
		iter->second = TF_REQUEST_MSG;
	}
	else
	{
		m_map_client_request.insert(iter,map<CLIENT_MSG,TRANS_FILE_MSG>::value_type(C_MSG,TF_REQUEST_MSG));
	}
}

/*******************************************************************************
  @brief  获得客户传输请求的相关信息
  
  @param strIP           发送方IP  
  @param wPort           发送方端口
  @param strName         发送文件或目录的名称
  @param ullTotalLen     发送文件或目录的总大小
  @return				 返回发送请求类型,只可能是以下三种值： 
				   TransFileRequest, TransDirRequest, CancelTransRequest
********************************************************************************/
DWORD CFtp::GetTransRequestInfo(const string& strIP, WORD wPort, CString& strName, ULONGLONG& ullTotalLen)
{
    TRANS_FILE_MSG* TF_REQUEST_MSG = FindClientRequestMsg(strIP, wPort);
	ASSERT(TF_REQUEST_MSG);
	ullTotalLen = TF_REQUEST_MSG->data_msg.ullLen;
	strName = (LPCTSTR)TF_REQUEST_MSG->data_msg.buf;
	return TF_REQUEST_MSG->ctrl_msg;
}

/*******************************************************************************
  @brief 接受客户的传输请求
  
  @param strIP           发送方IP  
  @param wPort           发送方端口
  @param lpSavePath      文件或目录的保存路径
*******************************************************************************/
void CFtp::AcceptTransRequest(const string& strIP, WORD wPort, LPCTSTR lpSavePath, bool bDir)
{
   	PSOCKET_OBJ pSocketObj = GetSocketObj(strIP, wPort, 0);
	ASSERT(pSocketObj);

	TRANS_FILE_MSG* PTF_SEND_MSG = new TRANS_FILE_MSG;

	PTF_SEND_MSG->ctrl_msg = (bDir ? DirRequestBeAccept : FileRequestBeAccept);
	memcpy(PTF_SEND_MSG->data_msg.buf, lpSavePath, sizeof(TCHAR)*_tcslen(lpSavePath));

	CTCPNetBuf SendBuf((PBYTE)PTF_SEND_MSG, sizeof(TRANS_FILE_MSG));
    SendData(SendBuf, strIP, wPort, false);
}

/*******************************************************************************
  @brief  拒绝客户的传输请求
  
  @param strIP           发送方IP  
  @param wPort           发送方端口
*******************************************************************************/
void CFtp::RefuseTransRequest(const string& strIP, WORD wPort, bool bDir)
{
	PSOCKET_OBJ pSocketObj = GetSocketObj(strIP, wPort, 0);
	ASSERT(pSocketObj);

	TRANS_FILE_MSG* PTF_SEND_MSG = new TRANS_FILE_MSG;

	PTF_SEND_MSG->ctrl_msg = (bDir ? DirRequestBeRefuse : FileRequestBeRefuse);
	CTCPNetBuf SendBuf((PBYTE)PTF_SEND_MSG, sizeof(TRANS_FILE_MSG));
	SendData(SendBuf, strIP, wPort, false);

	TRACE("RefuseTransRequest: %p \n", pSocketObj);
}

/*******************************************************************************
  @brief  取消传输任务
  
  @param TransmitTask  任务描述结构体
*******************************************************************************/
void CFtp::CancelTransmit(const TRANSMIT_TASK& TransmitTask)
{
	bool   bSrcDir = false, bDestDir = false;
	if (!IsDirectory(TransmitTask.strSrc, bSrcDir))
	{
		return ;
	}
	bool bLocal = TransmitTask.IsLocalTask();
	if (bLocal)
	{
		if (!IsDirectory(TransmitTask.strDest, bDestDir))
		{
			return;
		}
	}
	TRANSMIT_TASK&  Task = const_cast<TRANSMIT_TASK&>(TransmitTask);
	if (bSrcDir) 
	{
		if (Task.strSrc.ReverseFind(_T('\\')) != Task.strSrc.GetLength()-1)
		{
			Task.strSrc += _T('\\');
		}
	}
	if (bLocal)
	{
		if (Task.strDest.ReverseFind(_T('\\')) != Task.strDest.GetLength()-1)
		{
			Task.strDest += _T('\\');
		}
	}
    THREAD_MSG* T_MSG = FindThreadMsg(TransmitTask);
	 //双态检测T_MSG
    if (T_MSG)
	{
	    HANDLE hThread = T_MSG->hThread; 
		InterlockedExchange((volatile long*)&T_MSG->bExit, TRUE);
		if (!bLocal)
		   CloseSocketObj(GetSocketObj(TransmitTask.strIP, TransmitTask.wPort, T_MSG->dwThreadID));
		//当窗口无效时,传输线程内会释放T_MSG指向的内存,因此不可用T_MSG->hThread
		if (hThread)
		{
            WaitForSingleObject(hThread, INFINITE);  
		    CloseHandle(hThread);  
		    CAutoLockEx AutoLock(m_cs_task_thread);
			if (T_MSG = FindThreadMsg(TransmitTask))
			{
                T_MSG->hThread = NULL;
			}
		}
	}
}

/*******************************************************************************
  @brief 传输任务

  * 用来传输文件或目录,根据传入的源路径能自动识别是传文件还是目录,异步操作

  @param TransmitTask 传输结构体
  @param hWnd         通知窗口句柄
  @return             true表示操作成功,false表示操作失败
*******************************************************************************/
DWORD CFtp::TransmitTask(const TRANSMIT_TASK& TransmitTask, HWND hWnd)
{
	TASK_INFO* T_INFO;   const TRANSMIT_TASK* T_TASK;
	if (TransmitTask.strSrc.IsEmpty()) 
	{
		return errorSrcEmpty;
	}
	bool bLocal = TransmitTask.IsLocalTask();
	if (bLocal)
	{
		if (TransmitTask.strDest.IsEmpty())
		{
			return errorDestEmpty;
		}
	}
	if (hWnd)
	{
		if (!::IsWindow(hWnd))
		{
			return errorInvalidHwnd;
		}
	}
	TransmitFlag flag = File;
	bool  bSrcDir = false, bDestDir = false;
	if (!IsDirectory(TransmitTask.strSrc, bSrcDir))
	{
		return errorSrcInvalid;
	}
	if (bLocal)
	{
		if (!IsDirectory(TransmitTask.strDest, bDestDir))
		{
			return errorSrcInvalid;
		}
	}
	TRANSMIT_TASK& Task = const_cast<TRANSMIT_TASK&>(TransmitTask);
	if (bSrcDir) 
	{
		flag = Dir;
		if (Task.strSrc.ReverseFind(_T('\\')) != Task.strSrc.GetLength()-1)
		{
			Task.strSrc += _T('\\');
		}
	}
	if (bDestDir) 
	{
		if (Task.strDest.ReverseFind(_T('\\')) != Task.strDest.GetLength()-1)
		{
			Task.strDest += _T('\\');
		}
	}
	if (FindTransmitTaskInfo(TransmitTask, T_TASK, T_INFO))
	{
        return errorTransIng;   
	}
	
	TRANSMIT_MSG* pMSG= new TRANSMIT_MSG(flag, TransmitTask, this);
	DWORD  dwThreadID;
	HANDLE hThread = CreateThread(NULL, 0, TransmitThread, pMSG, CREATE_SUSPENDED, &dwThreadID);
	if(NULL != hThread)
	{
		AddTransmitTaskThreadMsg(TransmitTask, THREAD_MSG(hThread, dwThreadID, hWnd));
		AddTransmitTaskTransInfo(TransmitTask, new TASK_INFO());
		ResumeThread(hThread);
		return errorNo;
	}
    delete pMSG; 	return errorMemoryLack;  
}

DWORD CFtp::TransmitFile(TRANSMIT_MSG* pMSG)
{
    if (pMSG->RootTask->IsLocalTask())
	{
		return TransmitLocalFile(pMSG);
	}
	else
	{
        return TransmitNetFile(pMSG);
	}
}

DWORD CFtp::TransmitLocalFile(TRANSMIT_MSG* pMSG)
{
	const TRANSMIT_TASK* T_TASK;  TASK_INFO* T_INFO; 
	FindTransmitTaskInfo(*(pMSG->RootTask), T_TASK, T_INFO);
	ASSERT(T_INFO && T_TASK);

	CString &strSrc = pMSG->TransmitTask.strSrc;
	CString strDestFile = pMSG->TransmitTask.strDest;
	strDestFile += strSrc.Right(strSrc.GetLength()-strSrc.ReverseFind(_T('\\'))-1);

	T_INFO->SetTransFile(pMSG->TransmitTask.strSrc, strDestFile);
	T_INFO->SetFileLen(0, 0);

	HANDLE hSrcFile = CreateFile(pMSG->TransmitTask.strSrc,GENERIC_READ,FILE_SHARE_READ,
		NULL,OPEN_EXISTING,NULL, NULL);                
	if (hSrcFile == INVALID_HANDLE_VALUE)
	{
		return errorOpenFail;
	}
	LARGE_INTEGER FileSize;
	FILETIME      ftLastWrite;
	GetFileSizeEx(hSrcFile, &FileSize);
	GetFileTime(hSrcFile, NULL, NULL, &ftLastWrite);

	TRANS_FILE_MSG  TF_SEND_MSG, TF_RECV_MSG;
	TF_SEND_MSG.data_msg.LastWriteTime = ftLastWrite;
	TF_SEND_MSG.data_msg.ullLen = FileSize.QuadPart;
	memcpy(TF_SEND_MSG.data_msg.buf, strDestFile.GetBuffer(), strDestFile.GetLength()*sizeof(TCHAR));

	strDestFile.ReleaseBuffer();
	T_INFO->SetFileLen(FileSize.QuadPart, 0);

	THREAD_MSG* TH_MSG = FindThreadMsg(*(pMSG->RootTask));
	ASSERT(TH_MSG);

	if (CoverAllFile == TH_MSG->lUserSet)
		TF_SEND_MSG.ctrl_msg = CoverOneFile;
	else 
		TF_SEND_MSG.ctrl_msg = RequestOneFile;

	DWORD dwRet = errorNo;

	HANDLE hDestFile = INVALID_HANDLE_VALUE;
	if (RequestOneFile == TF_SEND_MSG.ctrl_msg)
	{
		bool bDir;
		if (IsDirectory(strDestFile, bDir))
		{
			if (JumpAllFile == TH_MSG->lUserSet) 
		    {
			    dwRet = errorUserJump;  
				goto clean;
		    }
			TF_RECV_MSG.ctrl_msg = FileIsExist;
			GetFileTimeByPath(strDestFile, NULL, NULL, &ftLastWrite);
			TF_RECV_MSG.data_msg.LastWriteTime = ftLastWrite;
			BOOL  bExit = FALSE;
			TF_RECV_MSG.data_msg.ullLen = GetDirSize(strDestFile, bExit);
			memcpy(TF_SEND_MSG.data_msg.buf, strDestFile.GetBuffer(), strDestFile.GetLength()*sizeof(TCHAR));
			strDestFile.ReleaseBuffer();

			StopTimer(TH_MSG);
			LRESULT UIRet = SendMessage(TH_MSG->hWnd, WM_TRANS_BREAK, (WPARAM)&TF_SEND_MSG, (LPARAM)&TF_RECV_MSG);
			StartTimer(TH_MSG, Elapse);
			if (CoverOneFile == UIRet) //覆盖同名文件
			{
			    TF_SEND_MSG.ctrl_msg = CoverOneFile;
			}
			else if (CoverAllFile == UIRet)  //覆盖所有同名文件
			{
			    TF_SEND_MSG.ctrl_msg = CoverOneFile; TH_MSG->lUserSet  = CoverAllFile;
			}
			else if (JumpOneFile == UIRet) //仅跳过当前文件,继续发送
			{
			    dwRet =  errorUserJump; 
				goto clean;
			}
			else if (JumpAllFile == UIRet) //跳过所有同名文件
			{
			    dwRet = errorUserJump; TH_MSG->lUserSet = JumpAllFile; 
				goto clean;
			}
			else if (CancelFile == UIRet)  //取消传输任务
			{
			    dwRet = errorUserCancel; 
				goto clean;
			}
			else
			{
			    dwRet = errorUserCancel;  
				goto clean;
			}
		}
	}
	//先创建所有目录
	int nError = SHCreateDirectoryEx(NULL, strDestFile.Left(strDestFile.ReverseFind(_T('\\'))), NULL);
	if (ERROR_PATH_NOT_FOUND == nError||ERROR_BAD_PATHNAME == nError||ERROR_FILENAME_EXCED_RANGE == nError)
	{
		dwRet = errorDestInvalid;
		goto clean;
	}
    hDestFile = CreateFile(strDestFile,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,
							CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if (INVALID_HANDLE_VALUE == hDestFile)
	{
		TF_RECV_MSG.ctrl_msg = FileIsUsed;
		StopTimer(TH_MSG);
		LRESULT UIRet = SendMessage(TH_MSG->hWnd, WM_TRANS_BREAK, (WPARAM)&TF_SEND_MSG, (LPARAM)&TF_RECV_MSG);
		StartTimer(TH_MSG, Elapse);
	    if (JumpOneFile == UIRet) //仅跳过当前文件,继续发送
	    {
		    dwRet =  errorUserJump; 
	    }
		else
		{
			dwRet = errorUserCancel;
		}
        goto clean;
	}
	SetFileTime(hDestFile, NULL, NULL, &TF_SEND_MSG.data_msg.LastWriteTime);
	for (DWORD dwReaded = 0, dwWrited = 0; ;)
	{
		if (InterlockedExchangeAdd((volatile long*)&TH_MSG->bExit, 0))
		{
			dwRet = errorUserCancel; 	break;
		}
		BOOL bResult = ReadFile(hSrcFile, TF_SEND_MSG.data_msg.buf, SendBufLen, &dwReaded, NULL);
		if ( bResult && dwReaded)
		{
			if (!WriteFile(hDestFile, TF_SEND_MSG.data_msg.buf, dwReaded, &dwWrited, NULL))
			{
				dwRet = errorWriteFail; break;
			}
			T_INFO->AddTransLen(dwReaded);
		}
		else if (bResult && dwReaded == 0)
		{
			dwRet = errorNo;   break;
		}
		else
		{
			dwRet = errorReadFail; break;
		}
	}
clean:
	if (errorUserJump == dwRet)
	{
		T_INFO->AddTransLen(FileSize.QuadPart);
	}
	if (INVALID_HANDLE_VALUE != hSrcFile)
	{
		CloseHandle(hSrcFile);
	}
	if (INVALID_HANDLE_VALUE != hDestFile)
	{
		CloseHandle(hDestFile);
	}
    return dwRet;
}

DWORD CFtp::TransmitNetFile(TRANSMIT_MSG* pMSG)
{
	 const TRANSMIT_TASK* T_TASK;  TASK_INFO* T_INFO; 
	 FindTransmitTaskInfo(*(pMSG->RootTask), T_TASK, T_INFO);
	 ASSERT(T_INFO && T_TASK);

	 CString &strSrc = pMSG->TransmitTask.strSrc;
	 CString strDestFile = pMSG->TransmitTask.strDest;
	 if (Dir == pMSG->enFlag)
	 {
		 strDestFile += strSrc.Right(strSrc.GetLength()-strSrc.ReverseFind(_T('\\'))-1);
	 }
	
	 T_INFO->SetTransFile(pMSG->TransmitTask.strSrc, strDestFile);
	 T_INFO->SetFileLen(0, 0);

	 HANDLE hFile = CreateFile(pMSG->TransmitTask.strSrc,GENERIC_READ,FILE_SHARE_READ,
		                       NULL,OPEN_EXISTING,NULL, NULL);                
	 if (hFile == INVALID_HANDLE_VALUE)
	 {
		 return errorOpenFail;
	 }
	 LARGE_INTEGER FileSize;
	 FILETIME      ftLastWrite;
	 GetFileSizeEx(hFile, &FileSize);
	 GetFileTime(hFile, NULL, NULL, &ftLastWrite);
	 
	 TRANS_FILE_MSG  TF_SEND_MSG;
	 TF_SEND_MSG.data_msg.LastWriteTime = ftLastWrite;
	 TF_SEND_MSG.data_msg.ullLen = FileSize.QuadPart;
	 memcpy(TF_SEND_MSG.data_msg.buf, strDestFile.GetBuffer(), strDestFile.GetLength()*sizeof(TCHAR));
	 strDestFile.ReleaseBuffer();
	 T_INFO->SetFileLen(FileSize.QuadPart, 0);
     
	 PSOCKET_OBJ pSocketObj = GetSocketObj(pMSG->RootTask->strIP,pMSG->RootTask->wPort, GetCurrentThreadId());
	 ASSERT(pSocketObj);

	 THREAD_MSG* TH_MSG = FindThreadMsg(*(pMSG->RootTask));
	 ASSERT(TH_MSG);
	
	 if (CoverAllFile == TH_MSG->lUserSet)
		 TF_SEND_MSG.ctrl_msg = CoverOneFile;
	 else 
		 TF_SEND_MSG.ctrl_msg = RequestOneFile;

	 DWORD   dwRet = errorNo;
	 for (TRANS_FILE_MSG TF_RECV_MSG; ; )
	 {
		 if (!SendData(pSocketObj, &TF_SEND_MSG, sizeof(TRANS_FILE_MSG), SendDelay))
		 {
			 WSAGetLastError() == WSAENOTSOCK ? dwRet = errorUserCancel : dwRet = errorConnectBreak;
			 break;
		 }
         if (!RecvData(pSocketObj, &TF_RECV_MSG, sizeof(TRANS_FILE_MSG), RecvDelay))
		 {
			 WSAGetLastError() == WSAENOTSOCK ? dwRet = errorUserCancel : dwRet = errorConnectBreak;
			 break;
		 }
         if (TF_RECV_MSG.ctrl_msg == FileIsAccept)
		 {
             break;
		 }
		 //接收方同名文件已存在或被另一程序使用
		 else  
		 {
			 if (JumpAllFile == TH_MSG->lUserSet) 
			 {
				 dwRet = errorUserJump;  break;
			 }
			 if (!TH_MSG->hWnd)  
			 {
				 if (FileIsUsed == TF_RECV_MSG.ctrl_msg)
				 {
					 dwRet =  errorUserJump;  break;
				 }
				 continue;
 			 }
			 //目标文件路径无效,直接终止传输
			 if (FileInvalidPath == TF_RECV_MSG.ctrl_msg)
			 {
				 dwRet = errorDestInvalid; break;
			 }
			 StopTimer(TH_MSG);
			 LRESULT UIRet = SendMessage(TH_MSG->hWnd, WM_TRANS_BREAK, (WPARAM)&TF_SEND_MSG, (LPARAM)&TF_RECV_MSG);
			 StartTimer(TH_MSG, Elapse);
			 if (CoverOneFile == UIRet) //覆盖同名文件
			 {
				 TF_SEND_MSG.ctrl_msg = CoverOneFile;
			 }
			 else if (CoverAllFile == UIRet)  //覆盖所有同名文件
			 {
				 TF_SEND_MSG.ctrl_msg = CoverOneFile; TH_MSG->lUserSet  = CoverAllFile;
			 }
			 else if (JumpOneFile == UIRet) //仅跳过当前文件,继续发送
			 {
			     dwRet =  errorUserJump;  break;
			 }
			 else if (JumpAllFile == UIRet) //跳过所有同名文件
			 {
                 dwRet = errorUserJump; TH_MSG->lUserSet = JumpAllFile; break;
			 }
			 else if (CancelFile == UIRet)  //取消传输任务
			 {
				 dwRet = errorUserCancel;  break;
			 }
			 else
			 {
                 dwRet = errorUserCancel;  break;
			 }
		 }
	 }
     if (errorNo != dwRet) 
	 {
		T_INFO->AddTransLen(FileSize.QuadPart);
		CloseHandle(hFile);  return dwRet;
	 }

	 fd_set fdRead, fdWrite;  
	 FD_ZERO(&fdRead);  FD_ZERO(&fdWrite);
	
	 for (DWORD dwReaded = 0; ; )
	 {
		 FD_SET(pSocketObj->s, &fdRead);	 
		 FD_SET(pSocketObj->s, &fdWrite);
		 int nRet = select(0, &fdRead, &fdWrite, 0, 0);
		 if (nRet <= 0 )
		 {
			 WSAGetLastError() == WSAENOTSOCK ? dwRet = errorUserCancel : dwRet = errorConnectBreak;
			 RemoveSocketObj(pSocketObj); 
			 break;
		 }
		 if (FD_ISSET(pSocketObj->s, &fdRead))
		 {
			 TRANS_FILE_MSG TF_RECV_MSG;
			 if (!RecvData(pSocketObj, &TF_RECV_MSG, sizeof(TRANS_FILE_MSG)))
			 {
				 WSAGetLastError() == WSAENOTSOCK ? dwRet = errorUserCancel : dwRet = errorConnectBreak;
				 break;
			 }
             if (FileWriteFail == TF_RECV_MSG.ctrl_msg)
			 {
				 dwRet = errorWriteFail; break;  //接收方写文件失败,最可能原因是磁盘空间不够
			 }
		 }
		 if (FD_ISSET(pSocketObj->s, &fdWrite))
		 {
			 BOOL bResult = ReadFile(hFile, TF_SEND_MSG.data_msg.buf, SendBufLen, &dwReaded, NULL);
			 if ( bResult && dwReaded)
			 {
				 TF_SEND_MSG.ctrl_msg = SendFile;  TF_SEND_MSG.data_msg.ullLen  = dwReaded;
				 if (!SendData(pSocketObj, &TF_SEND_MSG, sizeof(TRANS_FILE_MSG)))
				 {
					 WSAGetLastError() == WSAENOTSOCK ? dwRet = errorUserCancel : dwRet = errorConnectBreak;
					 break;
				 }
				 T_INFO->AddTransLen(dwReaded);
			 }
			 else if (bResult && dwReaded == 0)
			 {
				 dwRet = errorNo;   break;
			 }
			 else
			 {
				 dwRet = errorReadFail; break;
			 }
		 }
	 }
	 CloseHandle(hFile); return dwRet;
}

/*******************************************************************************
	@brief 实现传送单个目录
    
	*  使用了递归方法传送子目录, 当TransmitFile, TransmitDirImpl返回值不为errorNo时,则传输结束                                                                     

	@param pMSG 传送消息结构体
	@return     传送结果,传送成功返回errorNo,否则返回出错代码                                                                   
*******************************************************************************/
DWORD CFtp::TransmitDir(TRANSMIT_MSG* pMSG)
{
	 ASSERT(Dir == pMSG->enFlag);

	 CString strSrcDir = pMSG->TransmitTask.strSrc + _T("*.*");

     DWORD           dwRet = errorNo;
	 HANDLE          hFindFile;
	 WIN32_FIND_DATA findData; 
	 for(BOOL bResult = ((hFindFile = ::FindFirstFile(strSrcDir, &findData)) != INVALID_HANDLE_VALUE) ; bResult; 
		 bResult = ::FindNextFile(hFindFile, &findData))
	 {
		 if(findData.cFileName[0] == _T('.')) 
		 {
			 continue;
		 }
		 TRANSMIT_MSG  TM_MSG(*pMSG);
		 if(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		 {
			 //内部加上'\\'
			 TM_MSG.TransmitTask.strSrc  += findData.cFileName + (CString)_T("\\");
			 TM_MSG.TransmitTask.strDest += findData.cFileName + (CString)_T("\\");
			 dwRet = TransmitDir(&TM_MSG);
			 if (errorUserJump == dwRet)
			 {
				 continue;
			 }
			 if (errorNo != dwRet) break; 
		 }
		 else
		 {
			 TM_MSG.TransmitTask.strSrc  += findData.cFileName;
			 dwRet = TransmitFile(&TM_MSG);
			 if (errorUserJump == dwRet)
			 {
				 continue;
			 }
			 if (errorNo != dwRet) break; 
		 }
	 }
	 ::FindClose(hFindFile);   return dwRet;
}

DWORD CFtp::WaitForDestPrcoessTransRequest(TRANSMIT_MSG* TM_MSG, const TRANSMIT_TASK* T_TASK, TRANSMIT_INFO* T_INFO, THREAD_MSG* TH_MSG)
{
	PSOCKET_OBJ pSocketObj = NULL;
 
	//以下代码支持用户界面的重连功能
	for (bool bTryConnect = true;bTryConnect;)
	{
		T_INFO->dwError = errorConnectIng; 
		TransmitNotify(TRANS_BEGIN,TH_MSG,T_TASK,T_INFO);

		pSocketObj = Connect(T_TASK->strIP, T_TASK->wPort, ConnectDelay);
		if (pSocketObj)  break;
		WSAGetLastError() == WSAENOTSOCK ? T_INFO->dwError = errorUserCancel : T_INFO->dwError = errorConnectFail;
		
		if (TH_MSG->hWnd && IsWindow(TH_MSG->hWnd) && errorConnectFail == T_INFO->dwError)
		{
		    int ret = SendMessage(TH_MSG->hWnd, WM_TRANS_BEGIN, (WPARAM)T_TASK, (LPARAM)T_INFO);
			if (IDCANCEL == ret) bTryConnect = false;
		}
		else
		{
			bTryConnect = false;
		}
		if (!bTryConnect) return T_INFO->dwError = errorUserCancel;
	}
    
	T_INFO->dwError = errorConnectOK;
	TransmitNotify(TRANS_BEGIN,TH_MSG,T_TASK,T_INFO);

	//发送文件或目录请求给对方,并传送文件名或目录名
	TRANS_FILE_MSG  TF_SEND_MSG;
	CString  strName;
	if (File == TM_MSG->enFlag)
	{
		TF_SEND_MSG.ctrl_msg = TransFileRequest;
		strName = T_TASK->strSrc.Right(T_TASK->strSrc.GetLength()-T_TASK->strSrc.ReverseFind(_T('\\')) - 1);
	}
	else 
	{
		TF_SEND_MSG.ctrl_msg = TransDirRequest;	
		strName = T_TASK->strSrc;
		strName.Delete(strName.ReverseFind(_T('\\')));
        int nIndex = strName.ReverseFind(_T('\\'));
		if (-1 != nIndex)
		{
          strName = strName.Right(strName.GetLength()-nIndex - 1);
		}
	}
	TF_SEND_MSG.data_msg.ullLen = T_INFO->GetTotalLen();
	memcpy(TF_SEND_MSG.data_msg.buf,strName.GetBuffer(),strName.GetLength()*sizeof(TCHAR));
	strName.ReleaseBuffer();

	if (!SendData(pSocketObj, &TF_SEND_MSG, sizeof(TRANS_FILE_MSG), SendDelay))
	{
		WSAGetLastError() == WSAENOTSOCK ? T_INFO->dwError = errorUserCancel : T_INFO->dwError = errorConnectBreak;
		return T_INFO->dwError;
	}
	
	fd_set  fdRead;
	FD_ZERO(&fdRead);

	for (TRANS_FILE_MSG TF_RECV_MSG; ; )
	{
		FD_SET(pSocketObj->s, &fdRead);
		int nRet = select(0, &fdRead, NULL, NULL, NULL);
		if(SOCKET_ERROR == nRet)
		{
            WSAGetLastError() == WSAENOTSOCK ? T_INFO->dwError = errorUserCancel : T_INFO->dwError = errorConnectBreak;
			RemoveSocketObj(pSocketObj); 
			return T_INFO->dwError;
		}
		if (FD_ISSET(pSocketObj->s, &fdRead))
		{
			if (!RecvData(pSocketObj, &TF_RECV_MSG, sizeof(TRANS_FILE_MSG)))
			{
				WSAGetLastError() == WSAENOTSOCK ? T_INFO->dwError = errorUserCancel : T_INFO->dwError = errorConnectBreak;
				return T_INFO->dwError;
			}
			if (FileRequestBeRefuse == TF_RECV_MSG.ctrl_msg)
			{
				T_INFO->dwError = errorRefuseFileRequest; break;
			}
			else if (DirRequestBeRefuse == TF_RECV_MSG.ctrl_msg)
			{
				T_INFO->dwError = errorRefuseDirRequest;  break;
			}
			else if (DirRequestBeAccept==TF_RECV_MSG.ctrl_msg || FileRequestBeAccept==TF_RECV_MSG.ctrl_msg)
			{
				//保存接收方保存路径名
				TRANSMIT_TASK* pTask = const_cast<TRANSMIT_TASK*>(T_TASK);
				pTask->strDest = (LPCTSTR)TF_RECV_MSG.data_msg.buf; 
				if (DirRequestBeAccept==TF_RECV_MSG.ctrl_msg && pTask->strDest.ReverseFind(_T('\\')) != pTask->strDest.GetLength() - 1)
				{
					pTask->strDest += _T('\\');
				}
				TM_MSG->RootTask->strDest = pTask->strDest;
				T_INFO->dwError = errorNo;
				break;
			}
		}
	}
	if (errorNo != T_INFO->dwError)
	{
        RemoveSocketObj(pSocketObj); 
	}
	return T_INFO->dwError;  
}

/*******************************************************************************
  @brief  传输线程   
  
*******************************************************************************/
DWORD WINAPI CFtp::TransmitThread(LPVOID lParam)
{
	TRACE("TransmitThread is running..... \n");

	TRANSMIT_MSG* TM_MSG = (TRANSMIT_MSG*)lParam;
	CFtp* pThis = (CFtp*)TM_MSG->pFtp;
	
	const TRANSMIT_TASK* T_TASK;  TASK_INFO* T_INFO; 
	pThis->FindTransmitTaskInfo(TM_MSG->TransmitTask, T_TASK, T_INFO);
	ASSERT(T_TASK && T_INFO); 
	
	THREAD_MSG* TH_MSG = pThis->FindThreadMsg(TM_MSG->TransmitTask);
	ASSERT(TH_MSG);
	
	if (File == TM_MSG->enFlag)
	{
		T_INFO->TM_INFO.dwError = errorCalcingFileSize;
	}
	else
	{
		T_INFO->TM_INFO.dwError = errorCalcingDirSize;
	}
    pThis->TransmitNotify(TRANS_BEGIN, TH_MSG, T_TASK, &T_INFO->TM_INFO);
	T_INFO->SetTotalLen(GetDirSize(TM_MSG->TransmitTask.strSrc, TH_MSG->bExit));
	if (InterlockedExchangeAdd((volatile long*)&TH_MSG->bExit, 0))
	{
		T_INFO->TM_INFO.dwError = errorUserCancel;
	}
	else
	{
	    T_INFO->TM_INFO.dwError = errorNo;
	}

    //等待对方处理传输请求 
	bool bLocal = T_TASK->IsLocalTask();
	if (!bLocal)
	{
		pThis->WaitForDestPrcoessTransRequest(TM_MSG, T_TASK, &T_INFO->TM_INFO, TH_MSG);
	}
	pThis->TransmitNotify(TRANS_BEGIN, TH_MSG, T_TASK, &T_INFO->TM_INFO);
	pThis->StartTimer(TH_MSG, Elapse);

	if (errorNo == T_INFO->TM_INFO.dwError)
	{
		if (File == TM_MSG->enFlag)
		{
			T_INFO->TM_INFO.dwError = pThis->TransmitFile(TM_MSG);
		}
		else 
		{
			T_INFO->TM_INFO.dwError = pThis->TransmitDir(TM_MSG);
		}
	}
	//传输结束关闭连接
	if (!bLocal)
	{
		PSOCKET_OBJ pSocketObj = pThis->GetSocketObj(T_TASK->strIP, T_TASK->wPort, GetCurrentThreadId());
		if (pSocketObj) pThis->RemoveSocketObj(pSocketObj);
	}
	pThis->StopTimer(TH_MSG);
	pThis->TransmitNotify(TRANS_END, TH_MSG, T_TASK, &T_INFO->TM_INFO);

	if ((errorNo == T_INFO->TM_INFO.dwError || errorUserJump == T_INFO->TM_INFO.dwError)
		&& TM_MSG->TransmitTask.bDelete)
	{
		if (Dir == TM_MSG->enFlag) 
		{
			TM_MSG->TransmitTask.strSrc.Delete(TM_MSG->TransmitTask.strSrc.ReverseFind(_T('\\')));
		}
		else
		{
			TM_MSG->TransmitTask.strSrc += _T('\0');
		}
		CFtp::DeleteDirectory(TM_MSG->TransmitTask.strSrc);
	}

	if (NULL == TH_MSG->hWnd || !IsWindow(TH_MSG->hWnd))
	{
	    pThis->RemoveTransmitTask(TM_MSG->TransmitTask);
	}
	delete TM_MSG;

	TRACE("TransmitThread is exit..... \n");
	return 0;
}

void CFtp::TransmitNotify(NotifyState State, THREAD_MSG* T_MSG, const TRANSMIT_TASK* T_TASK, TRANSMIT_INFO* T_INFO)
{
	if (TRANS_BEGIN == State)
	{
	   if (T_MSG->hWnd && IsWindow(T_MSG->hWnd))
           PostMessage(T_MSG->hWnd, WM_TRANS_BEGIN, (WPARAM)T_TASK, (LPARAM)T_INFO);  
	}
	else 
	{
	   if(T_MSG->hWnd && IsWindow(T_MSG->hWnd))
           PostMessage(T_MSG->hWnd, WM_TRANS_END, (WPARAM)T_TASK, (LPARAM)T_INFO);  
	}
}

void CFtp::StartTimer(THREAD_MSG* T_MSG, UINT uElapse)
{
	if (T_MSG->hWnd)
	   T_MSG->uTimerID = SetTimer(T_MSG->hWnd, T_MSG->uTimerID, uElapse, (TIMERPROC)m_thunk(this, &CFtp::TimerProc, 0, 0));
}

void CFtp::StopTimer(THREAD_MSG* T_MSG)
{
	if (T_MSG->hWnd)
       KillTimer(T_MSG->hWnd, T_MSG->uTimerID);
}

void CFtp::TimerProc(HWND hWnd, UINT uMsg, UINT idEvent, DWORD dwTime)
{
	const TRANSMIT_TASK* T_TASK ;    TASK_INFO* T_INFO ;
	THREAD_MSG* TH_MSG = FindTaskInfoMatchHwnd(hWnd, T_TASK, T_INFO);
	ASSERT(TH_MSG && T_TASK && T_INFO);

	TH_MSG->dwTime += Elapse;
	T_INFO->ComputeTransSpeed(TH_MSG->dwTime);
	PostMessage(hWnd, WM_TRANS_ING, (WPARAM)T_TASK, (LPARAM)&T_INFO->TM_INFO);
}

////////////////////////////////////////////////////////////////////////////////////////////////
void CFtp::OnReceiveBegin(DWORD dwTrans, const string strIP, WORD wPort)
{
	ASSERT(dwTrans == sizeof(TRANS_FILE_MSG));

    RECV_FILE_MSG* R_F_MSG = FindFileInfoOfClient(strIP, wPort);
	if (R_F_MSG)
	{
         R_F_MSG->dwRecv = 0;
		 memset(R_F_MSG->buf, 0, RECV_FILE_MSG::BUF_LEN);
	}
	else
	{
		 CAutoLockEx  AutoLock(m_cs_client_file);
		 m_map_client_file.insert(make_pair(CLIENT_MSG(strIP, wPort), RECV_FILE_MSG())); 
	}
	TRACE("OnReceiveBegin from %s_%d : %d bytes \r\n", strIP.c_str(), wPort, dwTrans);
}

void CFtp::OnReceive(const void* pData, DWORD dwTrans, const string strIP, WORD wPort)
{
    RECV_FILE_MSG* R_F_MSG = FindFileInfoOfClient(strIP, wPort);
	ASSERT(R_F_MSG);

    memcpy(&R_F_MSG->buf[R_F_MSG->dwRecv], pData, dwTrans);
	R_F_MSG->dwRecv += dwTrans;

	TRACE("OnReceive from %s_%d : %d bytes \r\n", strIP.c_str(), wPort, dwTrans);
}

void CFtp::OnReceiveEnd(DWORD dwTrans, const string strIP, WORD wPort)
{
	USES_CONVERSION; //edit by foyo99
	ASSERT(dwTrans == sizeof(TRANS_FILE_MSG));
	PSOCKET_OBJ pSocketObj = GetSocketObj(strIP, wPort, 0);
	
	RECV_FILE_MSG* R_F_MSG = FindFileInfoOfClient(strIP, wPort);
	ASSERT(R_F_MSG);
	
	TRANS_FILE_MSG* TF_RECV_MSG = (TRANS_FILE_MSG*)R_F_MSG->buf;
    
	TRANS_FILE_MSG  TF_SEND_MSG, TF_REQUEST_MSG;
	//客户方请求发送文件或目录
	if (TransFileRequest == TF_RECV_MSG->ctrl_msg || TransDirRequest == TF_RECV_MSG->ctrl_msg 
		|| CancelTransRequest == TF_RECV_MSG->ctrl_msg)
	{
		memcpy(&TF_REQUEST_MSG, TF_RECV_MSG, dwTrans);
		AddOrUpdateClientRequestMsg(strIP, wPort, TF_REQUEST_MSG);
		BOOL bOK = PostMessage(m_hTransRequestNotifyWnd, WM_TRANS_REQUEST, inet_addr(strIP.c_str()), (LPARAM)wPort);
	    TRACE4("PostMessage TransRequest is %d \n", bOK);
	}
	//客户方请求发送文件或覆盖文件
	else if (RequestOneFile == TF_RECV_MSG->ctrl_msg || CoverOneFile == TF_RECV_MSG->ctrl_msg)  
	{
		R_F_MSG->ullTotalLen = TF_RECV_MSG->data_msg.ullLen;   R_F_MSG->ullRecvLen = 0;
		
		//先创建所有目录
		CString  strFilePath = (LPCTSTR)TF_RECV_MSG->data_msg.buf; //这里有问题, TestFtp发来的文件名的
		//后一部分是UNICODE, 目录部分是ANSI!!!!

		int nError = SHCreateDirectoryEx(NULL, strFilePath.Left(strFilePath.ReverseFind(_T('\\'))), NULL);
		if (ERROR_PATH_NOT_FOUND == nError||ERROR_BAD_PATHNAME == nError||ERROR_FILENAME_EXCED_RANGE == nError)
		{
			//创建目录失败,发FileInvalidPath
			TF_SEND_MSG.ctrl_msg = FileInvalidPath;
			if (!SelectSendAll(pSocketObj, &TF_SEND_MSG, sizeof(TRANS_FILE_MSG), SendDelay))
			{
			}
			return;
		}
		if (RequestOneFile == TF_RECV_MSG->ctrl_msg)
		{
			bool  bIsDir;
			//强制覆盖
			if ( IsDirectory(strFilePath, bIsDir) )
			{
				//已存在同名文件, 发FileIsExist
				TF_SEND_MSG.ctrl_msg = FileIsExist;
				if( !bIsDir )
				{ 
					FILETIME ftLastWrite;
					GetFileTimeByPath(strFilePath, NULL, NULL, &ftLastWrite);
					TF_SEND_MSG.data_msg.LastWriteTime = ftLastWrite;
				}
				BOOL  bExit = FALSE;
				TF_SEND_MSG.data_msg.ullLen = GetDirSize(strFilePath, bExit);
				memcpy(TF_SEND_MSG.data_msg.buf, strFilePath.GetBuffer(), strFilePath.GetLength()*sizeof(TCHAR));
				strFilePath.ReleaseBuffer();

				if (!SelectSendAll(pSocketObj, &TF_SEND_MSG, sizeof(TRANS_FILE_MSG), SendDelay))
				{
				}
				return;
			}
		}
		R_F_MSG->hFile =  CreateFile(strFilePath,GENERIC_WRITE,FILE_SHARE_WRITE|FILE_SHARE_READ,NULL,
								CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);              
        if (INVALID_HANDLE_VALUE == R_F_MSG->hFile)
		{
			//接收方文件被占用, 发FileIsUsed
            TF_SEND_MSG.ctrl_msg = FileIsUsed;
			if (!SelectSendAll(pSocketObj, &TF_SEND_MSG, sizeof(TRANS_FILE_MSG), SendDelay))
			{
			}
			return;
		}
		else   //打开成功, 发FileIsAccept
		{
			SetFileTime(R_F_MSG->hFile, NULL, NULL, &TF_RECV_MSG->data_msg.LastWriteTime);
			TF_SEND_MSG.ctrl_msg = FileIsAccept;
			if (!SelectSendAll(pSocketObj, &TF_SEND_MSG, sizeof(TRANS_FILE_MSG), SendDelay))
			{
			}
		}
	}
	else if (SendFile == TF_RECV_MSG->ctrl_msg)    //传输文件内容
	{
		if (R_F_MSG->hFile == INVALID_HANDLE_VALUE)  return;
		DWORD  dwWrited;
        if (WriteFile(R_F_MSG->hFile, TF_RECV_MSG->data_msg.buf, TF_RECV_MSG->data_msg.ullLen, &dwWrited, NULL))
		{
			R_F_MSG->ullRecvLen += TF_RECV_MSG->data_msg.ullLen;
			if (R_F_MSG->ullRecvLen == R_F_MSG->ullTotalLen) //文件接受完成
			{
				CloseHandle(R_F_MSG->hFile);  
				R_F_MSG->hFile = INVALID_HANDLE_VALUE;
			}
		}
		else
		{
			//接收方写文件失败
			TF_SEND_MSG.ctrl_msg = FileWriteFail;
			if (!SelectSendAll(pSocketObj, &TF_SEND_MSG, sizeof(TRANS_FILE_MSG), SendDelay))
			{
			}
	    } 
	}
	else if (CancelFile == TF_RECV_MSG->ctrl_msg)     //发送方取消发送
	{
         CloseHandle(R_F_MSG->hFile); 
		 R_F_MSG->hFile = INVALID_HANDLE_VALUE;
	}
	TRACE("OnReceiveEnd from %s_%d : %d bytes \n", strIP.c_str(), wPort, dwTrans);
}

void CFtp::OnSendComplete(CTCPNetBuf& SendBuf,DWORD dwTrans, const string strIP, WORD wPort)
{
    TRACE("SendTo %s_%d OK: %d Bytes \n", strIP.c_str(), wPort, dwTrans);
}

void CFtp::OnSendError(DWORD dwError, const string strIP, WORD wPort)
{
	TRACE("SendTo %s_%d Error: %d \n", strIP.c_str(), wPort, dwError);
}

void CFtp::OnReceiveError(DWORD dwError, const string strIP, WORD wPort)
{
    //出现错误关闭文件
	TRACE("RecvFrom %s_%d Error is: %d \n", strIP.c_str(), wPort, dwError);
	RemoveClientFile(strIP, wPort);
    PostMessage(m_hRecvErrorWnd, WM_RECV_ERROR, inet_addr(strIP.c_str()),(LPARAM)wPort);
}
