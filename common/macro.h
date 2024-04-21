#ifndef _LIBCPLUS_COMMON_MACRO_H
#define _LIBCPLUS_COMMON_MACRO_H
//////////////////////////////////////////////////////////////////////////
/**
    @file macro.h

	*  ���ú궨��ͷ�ļ�
*/
//////////////////////////////////////////////////////////////////////////

#include <memory>
#include <stdarg.h>
#include <windows.h>
using namespace std;

//////////////////////////////////////////////////////////////////////////
/**
    @param x  ����
	*  ȡԪ�ظ�����
*/
//////////////////////////////////////////////////////////////////////////
#define NUM_ELEMENTS(x)   (sizeof((x)) / sizeof(0[(x)]))

/**
  @def SAFE_DESTROY_WINDOW(hWnd)     

  * ��ȫ�رմ��ں�                                                               
*/
#define  SAFE_DESTROY_WINDOW(hWnd) \
	if (hWnd && ::IsWindow(hWnd)) \
	   ::DestroyWindow(hWnd)
/**
  @brief TRACE4 ASCII�汾    
*/
inline void __TRACE4(const char * format,...)
{
	char Msg[8192] = { '\0' };
	va_list pArg;
	va_start(pArg, format);
	vsprintf(Msg, format, pArg);
	va_end(pArg);
	OutputDebugStringA(Msg);
}
/**
  @brief TRACE4 UNICODE�汾    
*/
inline void __TRACE4(const wchar_t * format,...)
{
	wchar_t Msg[8192] = { L'\0' };
	va_list pArg;
	va_start(pArg, format);
	vswprintf(Msg, format, pArg);
	va_end(pArg);
	OutputDebugStringW(Msg);
}

/**
@def TRACE4    
* ������Debug��Release��������Ϣ��                                                               
	*/
#ifdef _DEBUG
#define TRACE4 __TRACE4
#else
#define TRACE4 
#endif



#endif