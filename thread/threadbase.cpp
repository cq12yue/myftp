#include "stdafx.h"
#include "threadbase.h"
#include <cassert>

CThreadBase::CThreadBase():
m_hThread(NULL)
{

}

CThreadBase::~CThreadBase()
{
}

bool CThreadBase::Create(DWORD dwCreateFlags, UINT nStackSize, LPSECURITY_ATTRIBUTES lpSecurityAttrs)
{
	if (m_hThread) return true;
	m_hThread = CreateThread(lpSecurityAttrs, nStackSize, ThreadEntry, this, dwCreateFlags, &m_dwThreadID);
	if (NULL == m_hThread)	return false;
	return  true;
}

void CThreadBase::Resume()
{
	assert(m_hThread);
	ResumeThread(m_hThread);
}

void CThreadBase::Suspend()
{
	assert(m_hThread);
	SuspendThread(m_hThread);
}

void CThreadBase::Exit()
{
	Signal();
	if (m_hThread)   
	{
		WaitForSingleObject(m_hThread, INFINITE);
		CloseHandle(m_hThread); m_hThread = NULL;
	}
}

DWORD WINAPI CThreadBase::ThreadEntry(LPVOID lpParam)
{
	CThreadBase* pThread = (CThreadBase*)lpParam;
	if (pThread->Init())  
	   pThread->Run();
	return pThread->Clean();
}
