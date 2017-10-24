#pragma once
#include <system_error>
namespace GlacierLib
{
	enum class future_status
	{
		ready,
		timeout,
		deferred
	};
	enum class future_errc
	{
		broken_promise = 1,
		future_already_retrieved,
		promise_already_satisfied,
		no_state
	};
}
template<>
struct std::is_error_code_enum<GlacierLib::future_errc>
	: public std::true_type
{	// tests for error_code enumeration
};
namespace GlacierLib
{
	inline const char *_Future_error_map(int _Errcode) _NOEXCEPT
	{	// convert to name of future error
		switch (static_cast<future_errc>(_Errcode))
		{	// switch on error code value
		case future_errc::broken_promise:
			return ("broken promise");

		case future_errc::future_already_retrieved:
			return ("future already retrieved");

		case future_errc::promise_already_satisfied:
			return ("promise already satisfied");

		case future_errc::no_state:
			return ("no state");

		default:
			return (0);
		}
	}

	class _Future_error_category
		: public std::_Generic_error_category
	{	// categorize a future error
	public:
		_Future_error_category()
		{	// default constructor
			_Addr = _Future_addr;
		}

		virtual const char *name() const _NOEXCEPT
		{	// get name of category
			return ("future");
		}

		virtual std::string message(int _Errcode) const
		{	// convert to name of error
			const char *_Name = _Future_error_map(_Errcode);
			if (_Name != 0)
				return (_Name);
			else
				return (_Generic_error_category::message(_Errcode));
		}
	};
	inline const std::error_category& future_category() _NOEXCEPT
	{
		return (std::_Immortalize<_Future_error_category>());
	}

	inline std::error_code make_error_code(future_errc _Errno) _NOEXCEPT
	{	// make an error_code object
		return (std::error_code(static_cast<int>(_Errno), future_category()));
	}

	class future_error
		: public std::logic_error
	{	// future exception
	public:
		explicit future_error(std::error_code _Errcode) // internal, will be removed
			: logic_error(""), _Mycode(_Errcode)
		{	// construct from error code
		}

		explicit future_error(future_errc _Errno)
			: logic_error(""), _Mycode(make_error_code(_Errno))
		{	// construct from future_errc
		}

		const std::error_code& code() const _NOEXCEPT
		{	// return stored error code
			return (_Mycode);
		}

		const char *__CLR_OR_THIS_CALL what() const _NOEXCEPT
		{	// get message string
			return (_Future_error_map(_Mycode.value()));
		}

	private:
		std::error_code _Mycode;	// the stored error code
	};

}