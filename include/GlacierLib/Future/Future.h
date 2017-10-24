#pragma once
#include <GlacierLib\Future\FutureImpl.h>
#include <GlacierLib\Future\FunctionInfo.h>
#include <GlacierLib\Future\FutureThenHelp.h>
#include <GlacierLib\Future\FutureExecatorInterface.h>
namespace GlacierLib
{
	template<typename T>
	using share_future = future<T>;
	template<typename T>
	class future
	{
		friend detail::FutureHelp<T>;
		static_assert(!std::is_reference_v<T>, "can't support reference");
	public:
		using value_type = T;
	public:
		future(std::shared_ptr<FutureImpl<T>> t)
			:impl_(t)
		{

		}
		future()
			:future(std::make_shared<FutureImpl<T>>())
		{

		}
		future(std::nullptr_t)
			:future(nullptr)
		{

		}
		~future() = default;
		future(const future& other) = delete;
		future& operator=(const future& other) = delete;
		future(future&& other) = default;
		future& operator=(future&& other) = default;
	public:
		T get()
		{
			auto impl = impl_;
			if constexpr(std::is_void_v<T>)
			{
				impl->get(true);
			}
			else
			{
				return std::move(impl->get(true));
			}
		}
		bool vaild() const noexcept
		{
			return (!!impl_) && impl_->vaild();
		}
		bool is_ready() const
		{
			return vaild() && impl_->is_ready();
		}
		share_future<T> share()
		{
			return std::move(impl_);
		}
	private:
		template<typename Fn, typename result>
		future<result> then_implementation(Fn&& fn)
		{
			using function_info = detail::function_info<Fn>;
			if (!vaild())
			{
				throw future_error(future_errc::no_state);
			}

			auto new_impl = std::make_shared<FutureImpl<result>>();
			if (impl_->get_ready_callback())
			{
				throw future_error(future_errc::broken_promise);
			}
			else
			{
				std::function<void()> then_fn;
				if constexpr (function_info::arg_size == 0 || std::is_void_v<T>)
				{
					if constexpr(std::is_void_v<result>)
					{
						then_fn = [new_impl, fn = std::move(fn)]{
							try
						{
							fn();
							new_impl->set_value();
						}
						catch (...)
						{
							new_impl->set_exception(std::current_exception());
						}
						};
					}
					else
					{
						then_fn = [new_impl, fn = std::move(fn)]{
							try
						{
							auto result = fn();
							new_impl->set_value(std::move(result));
						}
						catch (...)
						{
							new_impl->set_exception(std::current_exception());
						}
						};
					}
				}
				else
				{
					using arg_type = typename function_info::arg_type;
					using first_arg = typename arg_type::argument_type;
					if constexpr(std::is_void_v<result>)
					{
						then_fn = [new_impl, fn = std::move(fn), impl = impl_]{
							try
						{

							fn(static_cast<first_arg>(detail::ThenHelp<T>(impl)));
							new_impl->set_value();
						}
						catch (...)
						{
							new_impl->set_exception(std::current_exception());
						}
						};
					}
					else
					{
						then_fn = [new_impl, fn = std::move(fn), impl = impl_]{
							try
						{
							auto result = fn(static_cast<first_arg>(detail::ThenHelp<T>(impl)));
							new_impl->set_value(std::move(result));
						}
						catch (...)
						{
							new_impl->set_exception(std::current_exception());
						}
						};
					}
				}
				impl_->set_ready_callback([then_fn = std::move(then_fn)]() mutable {
					add_then(std::move(then_fn));
				});
			}
			return new_impl;
		}
	public:
		template<typename Fn>
		decltype(auto) then(Fn&& fn)
		{
			using function_info = detail::function_info<Fn>;
			static_assert(function_info::value, "error function!");
			static_assert (detail::is_vaild_then_function<Fn, T>(), "error then function!");
			using result = typename function_info::result;
			return this->template then_implementation<Fn, result>(std::forward<Fn>(fn));
		}

		void wait() const
		{
			if (!vaild())
			{
				throw future_error(future_errc::no_state);
			}
			impl_->wait();
		}
		template<typename R, typename P>
		future_status wait_for(const std::chrono::duration<R, P>& rt) const
		{
			if (!vaild())
			{
				throw future_error(future_errc::no_state);
			}
			return impl_->wait_for(rt);
		}
		template<typename C, typename D>
		future_status wait_until(const std::chrono::time_point<C, D>& at) const
		{
			if (!vaild())
			{
				throw future_error(future_errc::no_state);
			}
			return impl_->wait_until(at);
		}
	private:
		std::shared_ptr<FutureImpl<T>> impl_;
	};

	template<typename T>
	class promise
	{
		friend detail::FutureHelp<T>;
	public:
		promise(std::shared_ptr<FutureImpl<T>> t)
			:impl_(t)
		{

		}
		promise()
			:promise(std::make_shared<FutureImpl<T>>())
		{

		}
		template<class _Alloc>
		promise(std::allocator_arg_t, const _Alloc& _Al)
			: promise(std::allocate_shared<FutureImpl<T>>(_Al))
		{
		}
		future<T> get_future()
		{
			return impl_;
		}

		template<class = std::enable_if_t<!std::is_void_v<T>>, typename T1>
		void set_value(T1&& val)
		{
			impl_->set_value(std::forward<T1>(val));
		}
		template<class = std::enable_if_t<std::is_void_v<T>>, typename... Empty>
		void set_value(Empty...)
		{
			impl_->set_value();
		}
		void set_exception(std::exception_ptr ptr)
		{
			impl_->set_exception(std::move(ptr));
		}
	private:
		std::shared_ptr<FutureImpl<T>> impl_;
	};

	template<class>
	class packaged_task;

	template<typename Result, typename... Args>
	class packaged_task<Result(Args...)>
	{
		friend detail::FutureHelp<Result>;
		using callback_function = std::function<Result(Args...)>;
		using future_impl = std::shared_ptr<FutureImpl<std::decay_t<Result>>>;
		using data_type = std::pair<callback_function, future_impl>;
	public:
		packaged_task(callback_function fn, future_impl impl)
			:data_(std::make_unique<data_type>(std::move(fn), std::move(impl)))
		{

		}
		packaged_task(callback_function fn)
			:packaged_task(std::move(fn), std::make_shared<FutureImpl<std::decay_t<Result>>>())
		{

		}

		void swap(packaged_task&& other) noexcept
		{
			std::swap(data_, other.data_);
		}
		bool valid() const noexcept
		{
			return get_future_impl()->vaild();
		}
		future<Result> get_future()
		{
			return get_future_impl();
		}
		void operator()(Args... args)
		{
			if (get_future_impl()->is_ready())
			{
				throw future_error(future_errc::promise_already_satisfied);
			}
			if constexpr (std::is_void_v<Result>)
			{
				get_function()(std::forward<Args>(args)...);
				get_future_impl()->set_value();
			}
			else
			{
				auto result = get_function()(std::forward<Args>(args)...);
				get_future_impl()->set_value(std::move(result));
			}
		}
		void reset()
		{
			packaged_task task(get_function());
			get_future_impl()->cancel();
			swap(task);
		}
	private:
		callback_function& get_function() const
		{
			return data_->first;
		}
		future_impl& get_future_impl() const
		{
			return data_->second;
		}
	private:
		std::unique_ptr<data_type> data_;
	};
}
#include <GlacierLib\Future\FutureAlgorithm.inl>