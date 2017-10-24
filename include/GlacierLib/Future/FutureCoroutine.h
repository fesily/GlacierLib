#pragma once
#include <experimental\coroutine>
#include <GlacierLib\Future\Future.h>
namespace std
{
	namespace experimental {
		template<class _Ty, class... _ArgTypes>
		struct coroutine_traits<GlacierLib::future<_Ty>, _ArgTypes...>
		{	// defines resumable traits for functions returning future<_Ty>
			struct promise_type
			{
				GlacierLib::promise<_Ty> _MyPromise;

				GlacierLib::future<_Ty> get_return_object()
				{
					return (_MyPromise.get_future());
				}

				bool initial_suspend() const
				{
					return (false);
				}

				bool final_suspend() const
				{
					return (false);
				}

				template<class _Ut>
				void return_value(_Ut&& _Value)
				{
					_MyPromise.set_value(_STD forward<_Ut>(_Value));
				}

				void set_exception(exception_ptr _Exc)
				{
					_MyPromise.set_exception(_STD move(_Exc));
				}
			};
		};

		template<class... _ArgTypes>
		struct coroutine_traits<GlacierLib::future<void>, _ArgTypes...>
		{	// defines resumable traits for functions returning future<void>
			struct promise_type
			{
				GlacierLib::promise<void> _MyPromise;

				GlacierLib::future<void> get_return_object()
				{
					return (_MyPromise.get_future());
				}

				bool initial_suspend() const
				{
					return (false);
				}

				bool final_suspend() const
				{
					return (false);
				}

				void return_void()
				{
					_MyPromise.set_value();
				}

				void set_exception(exception_ptr _Exc)
				{
					_MyPromise.set_exception(_STD move(_Exc));
				}
			};
		};
	}	// namespace experimental
#include <GlacierLib\Future\Future.h>
}
namespace GlacierLib
{
	template<class _Ty>
	bool await_ready(future<_Ty>& _Fut)
	{
		return (_Fut.is_ready());
	}

	template<class _Ty>
	void await_suspend(future<_Ty>& _Fut,
		std::experimental::coroutine_handle<> _ResumeCb)
	{
		_Fut.then([_ResumeCb]()
		{
			_ResumeCb();
		});
	}

	template<class _Ty>
	auto await_resume(future<_Ty>& _Fut) -> decltype(_Fut.get())
	{
		return (_Fut.get());
	}
}