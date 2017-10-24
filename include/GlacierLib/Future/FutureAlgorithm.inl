#pragma once
namespace GlacierLib
{
	namespace detail
	{
		template<typename T>
		struct FutureHelp
		{
			FutureHelp(future<T>& f)
				:impl_(f.impl_)
			{

			}
			FutureHelp(promise<T>& p)
				:impl_(p.impl_)
			{

			}
			FutureHelp(packaged_task<T>& p)
				:impl_(p.get_future_impl())
			{

			}
			std::shared_ptr<FutureImpl<T>> impl_;
		};
		template<typename T>
		FutureImpl<T> get_future_impl(future<T>& f)
		{
			return FutureHelp<T>{ f }.impl_;
		}
		template<typename T>
		FutureImpl<T> get_future_impl(promise<T>& f)
		{
			return FutureHelp<T>{ f }.impl_;
		}
		template<typename T>
		FutureImpl<T> get_future_impl(packaged_task<T>& f)
		{
			return FutureHelp<T>{ f }.impl_;
		}
		template<typename T>
		future<T> copy_future(future<T>& f)
		{
			return FutureHelp<T>{ f }.impl_;
		}
	}

	template<typename Fn>
	struct MakePackagedTaskHelper
	{
		using function_info = detail::function_info<Fn>;
		static_assert(function_info::value, "must is an function!");

		template<typename>
		struct ArgTypeHelp;
		template<typename... Args >
		struct ArgTypeHelp<std::_Arg_types<Args...>>
		{
			using type = packaged_task<typename function_info::result(Args...)>;
		};
		using arg_type = typename function_info::arg_type;
		using packaged_task_type = typename ArgTypeHelp<arg_type>::type;
	};

	template<typename Fn, typename helper = MakePackagedTaskHelper<Fn>>
	typename helper::packaged_task_type make_packaged_task(Fn&& fn)
	{
		return typename helper::packaged_task_type(std::forward<Fn>(fn));
	}

	template<typename T>
	future<std::decay_t<T>> make_ready_future(T&& value)
	{
		promise<std::decay_t<T>> promise;
		promise.set_value(std::forward<T>(value));
		return promise.get_future();
	}
	inline future<void> make_ready_future()
	{
		promise<void> promise;
		promise.set_value();
		return promise.get_future();
	}
	template<typename T>
	future<T> make_exceptional_future(std::exception_ptr ex)
	{
		promise<std::decay_t<T>> promise;
		promise.set_exception(ex);
		return promise.get_future();
	}
	template<typename T, typename E>
	inline future<void> make_exceptional_future(E ex)
	{
		promise<void> promise;
		promise.set_exception(std::make_exception_ptr(ex));
		return promise.get_future();
	}

	namespace detail
	{
		template<typename Tuple, typename Fn, size_t... Index >
		void for_each_tuple_impl(Tuple&& tuple, Fn&& fn, std::index_sequence<Index...>)
		{
			std::initializer_list<int>{(std::forward<Fn>(fn)(std::get<Index>(std::forward<Tuple>(tuple))), 0)...};
		}

		template<typename Fn, typename ... Types>
		void for_each_tuple(std::tuple<Types...>& tuple, Fn&& fn)
		{
			for_each_tuple_impl(tuple, std::forward<Fn>(fn), std::index_sequence_for<Types...>{});
		}

		template<typename Tuple, typename Fn, size_t... Index >
		void for_each_tuple_with_index_impl(Tuple&& tuple, Fn&& fn, std::index_sequence<Index...>)
		{
			std::initializer_list<int>{(std::forward<Fn>(fn)(std::get<Index>(std::forward<Tuple>(tuple)), Index), 0)...};
		}

		template<typename Fn, typename ... Types>
		void for_each_tuple_with_index(std::tuple<Types...>& tuple, Fn&& fn)
		{
			for_each_tuple_with_index_impl(tuple, std::forward<Fn>(fn), std::index_sequence_for<Types...>{});
		}

	}

	template <class InputIt>
	auto when_all(InputIt first, InputIt last)
		->future<std::vector<typename std::iterator_traits<InputIt>::value_type>>
	{
		using future_type = typename std::iterator_traits<InputIt>::value_type;

		struct WhenAllHelper : std::enable_shared_from_this<WhenAllHelper>
		{
			using futures_type = std::vector<future_type>;

			WhenAllHelper(size_t size)
			{
				futures_.reserve(size);
			}
			~WhenAllHelper()
			{
				promise_.set_value(std::move(futures_));
			}
			futures_type futures_;
			promise<futures_type> promise_;
		};
		auto helper = std::make_shared<WhenAllHelper>(std::distance(first, last));

		for (auto iter = first; iter != last; ++iter)
		{
			iter->then([helper](future_type& f)
			{
				helper->futures_.emplace_back(detail::get_future_impl(f));
			});
		}
		return helper->promise_.get_future();
	}

	template<class Collection>
	auto when_all(Collection&& c) -> decltype(when_all(c.begin(), c.end())) {
		return when_all(c.begin(), c.end());
	}

	template<class... Futures>
	auto when_all(Futures&&... futures)
		->future<std::tuple<std::decay_t<Futures>...>>
	{
		struct WhenAllHelper : std::enable_shared_from_this<WhenAllHelper>
		{
			using futures_type = std::tuple<std::decay_t<Futures>...>;

			WhenAllHelper(Futures&&... futures)
				:futures_(detail::copy_future(std::forward<Futures>(futures))...)
			{

			}

			~WhenAllHelper()
			{
				promise_.set_value(std::move(futures_));
			}
			futures_type futures_;
			promise<futures_type> promise_;
		};
		auto helper = std::make_shared<WhenAllHelper>(std::forward<Futures>(futures)...);
		detail::for_each_tuple(helper->futures_, [helper](auto& f)
		{
			f.then([helper] {});
		});
		return helper->promise_.get_future();
	}

	template<class Sequence >
	struct when_any_result
	{
		using sequence_type = Sequence;
		std::size_t index;
		Sequence futures;
	};

	template<class InputIt>
	auto when_any(InputIt first, InputIt last)
		->future<when_any_result<std::vector<typename std::iterator_traits<InputIt>::value_type>>>
	{
		using future_type = typename std::iterator_traits<InputIt>::value_type;

		struct WhenAllAny : std::enable_shared_from_this<WhenAllAny>
		{
			using result = when_any_result<std::vector<typename std::iterator_traits<InputIt>::value_type>>;

			WhenAllAny(size_t size)
				:has_index_(false)
			{
				result_.futures.reserve(size);
			}
			void notify(size_t index)
			{
				if (!has_index_)
				{
					// 如果有人锁了,说明不需要在继续了
					std::unique_lock<std::mutex> lock(mtx_, std::try_to_lock);
					if (lock && !has_index_)
					{
						has_index_ = true;
						result_.index = index;
						promise_.set_value(std::move(result_));
					}
				}
			}
			std::atomic_bool has_index_;
			std::mutex mtx_;
			result result_;
			promise<result> promise_;
		};

		auto helper = std::make_shared<WhenAllAny>(std::distance(first, last));

		for (auto iter = first; iter != last; ++iter)
		{
			helper->futures_.emplace_back(detail::get_future_impl(*iter));
			iter->then([helper, index = result.size() - 1](future_type& f) {
				helper->notify(index);
			});
		}
		return helper->promise_.get_future();
	}

	template<class Collection>
	auto when_any(Collection&& c) -> decltype(when_any(c.begin(), c.end()))
	{
		return when_any(c.begin(), c.end());
	}

	template<class... Futures>
	auto when_any(Futures&&... futures)
		->future<when_any_result<std::tuple<std::decay_t<Futures>...>>>
	{
		struct WhenAnyHelper : std::enable_shared_from_this<WhenAnyHelper>
		{
			using result = when_any_result<std::tuple<std::decay_t<Futures>...>>;

			WhenAnyHelper(Futures&&... futures)
				:has_index_(false), result_{ 0, typename result::sequence_type(detail::copy_future(std::forward<Futures>(futures))...) }
			{

			}

			void notify(size_t index)
			{
				if (!has_index_)
				{
					// 如果有人锁了,说明不需要在继续了
					std::unique_lock<std::mutex> lock(mtx_, std::try_to_lock);
					if (lock && !has_index_)
					{
						has_index_ = true;
						result_.index = index;
						promise_.set_value(std::move(result_));
					}
				}
			}
			std::atomic_bool has_index_;
			std::mutex mtx_;
			result result_;
			promise<result> promise_;
		};
		auto helper = std::make_shared<WhenAnyHelper>(std::forward<Futures>(futures)...);
		detail::for_each_tuple_with_index(helper->result_.futures, [helper](auto& f, size_t index)
		{
			f.then([helper, index] {
				helper->notify(index);
			});
		});
		return helper->promise_.get_future();
	}
}