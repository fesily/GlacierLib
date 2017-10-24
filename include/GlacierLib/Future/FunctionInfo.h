#pragma once
#include <type_traits>
namespace GlacierLib
{
	namespace detail
	{
		template<typename T>
		struct _function_info : std::false_type
		{

		};

#define FUNCTOR_INFO(CALL_OPT, X1, X2) \
		template<typename Ret, typename ... Args>\
			struct _function_info <Ret CALL_OPT (Args...)> :std::true_type\
		{\
			static constexpr size_t arg_size = sizeof...(Args); \
			static constexpr bool is_functor = false; \
			using result = Ret; \
			using arg_type = std::_Arg_types<Args...>; \
		};
		_NON_MEMBER_CALL(FUNCTOR_INFO, )

#undef  FUNCTOR_INFO                       

#define FUNCTOR_INFO(CALL_OPT, CV_OPT, REF_OPT) \
		template<typename Ret, typename T, typename ... Args>\
		struct _function_info <Ret (CALL_OPT T::*)(Args...) CV_OPT REF_OPT>:std::true_type\
		{\
			static constexpr size_t arg_size = sizeof...(Args);\
			static constexpr bool is_functor = true;\
			using result = Ret;\
			using arg_type =  std::_Arg_types<Args...>;\
			using class_type = T;\
		};

			_MEMBER_CALL_CV_REF(FUNCTOR_INFO)
#undef  FUNCTOR_INFO                       

			template<typename T, class = void>
		struct function_info1
			: _function_info<T>
		{
		};

		template<typename T>
		struct function_info1<T, std::void_t<decltype(&T::operator())>>
			: _function_info<decltype(&T::operator())>
		{

		};
		template<typename T>
		struct function_info
			: function_info1<std::decay_t<T>>
		{

		};
	}
}