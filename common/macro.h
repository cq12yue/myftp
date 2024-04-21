#ifndef _LIBCPLUS_COMMON_MACRO_H
#define _LIBCPLUS_COMMON_MACRO_H
//////////////////////////////////////////////////////////////////////////
/**
    @file macro.h

	*  常用宏定义头文件
*/
//////////////////////////////////////////////////////////////////////////

#include <memory>
#include <stdarg.h>
#include <windows.h>
using namespace std;

//////////////////////////////////////////////////////////////////////////
/**
    @param x  数组
	*  取元素个数宏
*/
//////////////////////////////////////////////////////////////////////////
#define NUM_ELEMENTS(x)   (sizeof((x)) / sizeof(0[(x)]))

/**
  @def SAFE_DESTROY_WINDOW(hWnd)     

  * 安全关闭窗口宏                                                               
*/
#define  SAFE_DESTROY_WINDOW(hWnd) \
	if (hWnd && ::IsWindow(hWnd)) \
	   ::DestroyWindow(hWnd)
/**
  @brief TRACE4 ASCII版本    
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
  @brief TRACE4 UNICODE版本    
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
* 适用于Debug和Release版的输出信息宏                                                               
	*/
#ifdef _DEBUG
#define TRACE4 __TRACE4
#else
#define TRACE4 
#endif



#endif