#ifndef _AUTOLOCK_H
#define _AUTOLOCK_H

#include <Windows.h>

class CCriticalSectionEx
{
public:
	CCriticalSectionEx()
	{
		InitializeCriticalSection(&m_cs);
	}
	~CCriticalSectionEx() 
	{
        DeleteCriticalSection(&m_cs);
	}

	void Lock()   
	{
		EnterCriticalSection(&m_cs); 
	} 
	void Unlock()
	{
		LeaveCriticalSection(&m_cs); 
	}

	operator CRITICAL_SECTION*() 
	{
	   return &m_cs;
	}

private:
	CCriticalSectionEx(const CCriticalSectionEx& );
	CCriticalSectionEx& operator= (const CCriticalSectionEx& );

private:
	CRITICAL_SECTION  m_cs;
};

class CAutoLockEx
{
public:
	CAutoLockEx(CCriticalSectionEx &cs, bool bLock = true):m_cs(cs),m_bLock(bLock)
	{
		if (m_bLock)    m_cs.Lock();
	}
	~CAutoLockEx()
	{
        if (m_bLock)    m_cs.Unlock();
	}

private:
	bool m_bLock;
	CCriticalSectionEx &m_cs;
};


#endif;