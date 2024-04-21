#ifndef _THREADBASE_H
#define _THREADBASE_H

/**
  @brief  线程抽象基类
  @class CThreadBase
                                                                 
*/
class CThreadBase
{
public:
	CThreadBase();
	virtual ~CThreadBase();

public:
	operator HANDLE() const  { 	return m_hThread; }

public:
	bool Create(DWORD dwCreateFlags = 0,UINT nStackSize = 0,LPSECURITY_ATTRIBUTES lpSecurityAttrs = NULL);
	void Resume();
	void Suspend();
	void Exit();

protected:
	virtual void Signal() = 0;
    virtual bool Init() = 0;
	virtual void Run() = 0;
    virtual DWORD Clean() = 0;

protected:
	HANDLE   m_hThread;
    DWORD    m_dwThreadID;

private:
	CThreadBase(const CThreadBase& Right);
	CThreadBase& operator=(const CThreadBase& Right);

private:
	static DWORD WINAPI ThreadEntry(LPVOID lpParam);
};

#endif
