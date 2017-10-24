#pragma once
#include <type_traits>
#include <exception>

namespace GlacierLib
{
	struct nullopt_t
	{
		explicit constexpr nullopt_t(int){}
	};
	class bad_optional_access
		: public std::exception
	{
	public:
		virtual const char* what() const _NOEXCEPT override
		{
			return ("Bad optional access");
		}

#if _HAS_EXCEPTIONS

#else /* _HAS_EXCEPTIONS */
	protected:
		virtual void _Doraise() const
		{	// perform class-specific exception handling
			_RAISE(*this);
		}
#endif /* _HAS_EXCEPTIONS */
	};
	
	constexpr nullopt_t nullopt{1};
	template<typename T>
	class optional
	{
		static_assert(!std::is_reference<T>::value);
	public:
		optional()
			:hasValue_(false)
		{
			
		}
		optional(nullopt_t)
			:optional()
		{
			
		}
	private:
		bool hasValue_;
		T data_;
	};
}

