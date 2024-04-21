#ifndef _ZTHUNK_H
#define _ZTHUNK_H

/**
  @class ZThunk
  @brief 通用ZThunk类,一种回调类成员函数的技术机制
  * 在网上相关代码的基础上,改进了封装以支持动态替换类成员函数                                                                
*/
template <class ToType, class FromType>
static void GetMemberFuncAddr(ToType& addr,FromType f)
{
	union 
	{
		FromType _f;
		ToType   _t;
	}ut;

	ut._f = f;

	addr = ut._t;
}

class ZThunk
{
public:
	ZThunk()
	{
		ThunkTemplate(m_addr1,m_addr2,0);
		memset(m_thunk,0,100);
		memcpy(m_thunk,(void*)m_addr1,m_addr2-m_addr1);
	}
	/*************************************************************************************************
	    重载operator()以方便调用替换类成员函数, 示例如下  

		SetTimer(NULL,0,uElapse,(TIMERPROC)m_ZThunk(this, &ClassName::MemberFun1));
		SetTimer(NULL,0,uElapse,(TIMERPROC)m_ZThunk(this, &ClassName::MemberFun2));
		m_ZThunk为ZThunk类对象, this是ClassName类的对象指针,该对象必须是全局有效的

	*************************************************************************************************/
	template<typename T>
	BYTE* operator()(void *obj, T t, WPARAM wParam = 0, LPARAM lParam = 0)
	{
		DWORD FuncAddr;
		GetMemberFuncAddr(FuncAddr, t);
		ReplaceCodeBuf(m_thunk,m_addr2-m_addr1,-1,(DWORD)(obj));
		ReplaceCodeBuf(m_thunk,m_addr2-m_addr1,-2,FuncAddr);

		m_wParam = wParam; m_lParam = lParam;
		return &m_thunk[0];
	}

	inline void GetParam(WPARAM& wParam, LPARAM &lParam)
	{
		wParam = m_wParam; 
		lParam = m_lParam;
	}

	static void ThunkTemplate(DWORD& addr1,DWORD& addr2,int calltype/*=0*/)
	{
		int flag = 0;
		DWORD x1,x2;

		if(flag)
		{
			__asm //__thiscall
			{
thiscall_1:	mov   ecx,-1;   //-1占位符,运行时将被替换为this指针.
				mov   eax,-2;   //-2占位符,运行时将被替换为CTimer::CallBcak的地址.
				jmp   eax;
thiscall_2:  ;
			}

			__asm //__stdcall
			{
stdcall_1:	push  dword ptr [esp]        ; //保存（复制）返回地址到当前栈中
				mov   dword ptr [esp+4], -1  ; //将this指针送入栈中，即原来的返回地址处
				mov   eax,  -2;
				jmp   eax                    ; //跳转至目标消息处理函数（类成员函数）
stdcall_2: ;
			}
		}

		if(calltype==0)//this_call
		{
			__asm
			{
				mov   x1,offset thiscall_1;  //取 Thunk代码段 的地址范围.
				mov   x2,offset thiscall_2 ;
			}
		}
		else
		{
			__asm
			{
				mov   x1,offset stdcall_1;   
				mov   x2,offset stdcall_2 ;
			}
		}
		addr1 = x1;
		addr2 = x2;
	}

	static void ReplaceCodeBuf(BYTE *code,int len, DWORD old,DWORD x)
	{
		int i=0;

		for(i=0;i<len-4;++i)
		{
			if(*((DWORD *)&code[i])==old)
			{
				*((DWORD *)&code[i]) = x;
				return ;
			}
		}
	}

private:
	BYTE    m_thunk[100];
	DWORD   m_addr1, m_addr2;
	WPARAM  m_wParam, m_lParam;
};

#endif
