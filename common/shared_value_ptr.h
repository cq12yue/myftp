#ifndef _SHARED_VALUE_PTR_H
#define _SHARED_VALUE_PTR_H

#include <Windows.h>

/*******************************************************************************
  @class shared_ptr 

  @brief 共享对象所有权的智能指针类
*******************************************************************************/

template < typename T >
class shared_ptr
{
private:
	class implement  // 实现类，引用计数
	{
	public: 
		implement(T* pp):p(pp),refs(1){}

		~implement(){ delete []p;}

		T* p; // 实际指针
		size_t refs; // 引用计数
	};
	implement* _impl;

public:
	explicit shared_ptr(T* p)
		:  _impl(new implement(p)){}

	virtual ~shared_ptr()
	{
		decrease();  // 计数递减
	}

	shared_ptr(const shared_ptr& rhs)
		:  _impl(rhs._impl)
	{
		increase();  // 计数递增
	}

	shared_ptr& operator=(const shared_ptr& rhs)
	{
		if (_impl != rhs._impl)  // 避免自赋值
		{
			decrease();  // 计数递减，不再共享原对象
			_impl=rhs._impl;  // 共享新的对象
			increase();  // 计数递增，维护正确的引用计数值
		}
		return *this;
	}

	T* operator->() const
	{
		return _impl->p;
	}

	T& operator*() const
	{
		return *(_impl->p);
	}

	T* get() const
	{
		return _impl->p;
	}
	size_t get_ref() const
	{
		if (_impl == 0) return 0;
		return (size_t)InterlockedExchangeAdd((volatile long*)&_impl->refs, 0);
	}

	void reset(T* p)
	{
         if (_impl && _impl->p == p) return;
		 decrease();
		 _impl = new implement(p);
	}
private:
	void decrease()
	{
		if (_impl && InterlockedDecrement((volatile long*)&_impl->refs)==0)
		{  // 不再被共享，销毁对象
			delete _impl; _impl = NULL;
		}
	}

	void increase()
	{
		if (_impl) InterlockedIncrement((volatile long*)&_impl->refs);
	}
};

template<typename T>
struct VPTraits
{
	static T* Clone( const T* p ) { return new T( *p ); }
};

template<typename T>
class ValuePtr
{
public:
	explicit ValuePtr( T* p = 0 ) : p_( p ) { }

	~ValuePtr() { delete p_; }

	T& operator*() const { return *p_; }

	T* operator->() const { return p_; }

	void Swap( ValuePtr& other ) { swap( p_, other.p_ ); }

	ValuePtr( const ValuePtr& other )
		: p_( CreateFrom( other.p_ ) ) { }

	ValuePtr& operator=( const ValuePtr& other )
	{
		ValuePtr temp( other );
		Swap( temp );
		return *this;
	}

	template<typename U>
	ValuePtr( const ValuePtr<U>& other )
		: p_( CreateFrom( other.p_ ) ) { }

	template<typename U>
	ValuePtr& operator=( const ValuePtr<U>& other )
	{
		ValuePtr temp( other );
		Swap( temp );
		return *this;
	}

private:
	template<typename U>
	T* CreateFrom( const U* p ) const
	{
		return p ? VPTraits<U>::Clone( p ) : 0;
	}
	template<typename U> friend class ValuePtr;

	T* p_;
};

#endif