#pragma once
#include <type_traits>
namespace GlacierLib
{
	template<typename T>
	class future;
	namespace detail
	{
		template<typename T1, typename T2, class = void>
		struct is_static_convertible : std::false_type
		{

		};

		template<typename T1, typename T2>
		struct is_static_convertible<T1, T2, std::void_t<decltype(static_cast<T2>(std::declval<T1>()))>> : std::true_type
		{

		};
		/*
		* 1.禁止基类转向子类
		* 2.static_cast可以转化的即可
		*/
		template<typename T1, typename T2>
		constexpr bool is_convertible()
		{
			using t1 = std::decay_t<T1>;
			using t2 = std::decay_t<T2>;
			if constexpr(std::is_same_v<t1, t2>)
			{
				return true;
			}
			else if constexpr(std::is_base_of_v<t1, t2>
				|| (std::is_pointer_v<t1> && std::is_pointer_v<t2> && std::is_base_of_v<std::remove_pointer_t<t1>, std::remove_pointer_t<t2>>))
			{
				return false;
			}
			else
			{
				return is_static_convertible<T1, T2>::value;
			}
		}


		template<typename Fn, typename T>
		constexpr bool is_vaild_then_function()
		{
			using function_info = function_info<Fn>;
			if constexpr(function_info::arg_size > 1)
			{
				return false;
			}
			else if constexpr(function_info::arg_size == 0)
			{
				return true;
			}
			else
			{
				using arg_type = typename function_info::arg_type;
				using first_arg = typename arg_type::argument_type;
				return is_convertible<T, first_arg>() || std::is_same_v<std::decay_t<first_arg>, future<T>>;
			}
		}
		template<typename T>
		struct ThenHelp;

		template<typename T, bool>
		struct ThenHelpBase;

		template<typename T>
		struct ThenHelpBase<T, false>
		{
			ThenHelp<T>* get_derive()
			{
				return static_cast<ThenHelp<T>*>(this);
			}
			operator T&() &&
			{
				return get_derive()->futureImpl_->get(true);
			}
			template<typename T1>
			operator T1&() &&
			{
				static_assert(is_convertible<T, T1>(), "can't convertible !");
				return static_cast<T1&>(get_derive()->futureImpl_->get(true));
			}
		};

		template<typename T>
		struct ThenHelpBase<T, true>
		{
			ThenHelp<T>* get_derive()
			{
				return static_cast<ThenHelp<T>*>(this);
			}

			operator T() &&
			{
				return get_derive()->futureImpl_->get(true);
			}

			template<typename T1>
			operator T1() &&
			{
				return static_cast<T1>(get_derive()->futureImpl_->get(true));
			}

		};

		template<typename T>
		struct ThenHelp : ThenHelpBase<T, std::is_arithmetic_v<std::decay_t<T>>>
		{
			ThenHelp(std::shared_ptr<FutureImpl<T>> future)
				:futureImpl_(std::move(future)),
				future_(futureImpl_)
			{

			}
			explicit operator future<T>& () &&
			{
				return future_;
			}
			explicit operator future<T>() &&
			{
				return future<T>(futureImpl_);
			}
			std::shared_ptr<FutureImpl<T>> futureImpl_;
			future<T> future_;
		};

		template<typename T>
		struct FutureHelp;
	}
}