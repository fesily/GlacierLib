#pragma once 
#include <boost/asio/async_result.hpp>
#include <GlacierLib\Future\Future.h>
namespace boost
{
	namespace asio
	{
		template <typename Allocator = std::allocator<void> >
		class use_future_t
		{
		public:
			/// The allocator type. The allocator is used when constructing the
			/// @c std::promise object for a given asynchronous operation.
			typedef Allocator allocator_type;

			/// Construct using default-constructed allocator.
			constexpr use_future_t()
			{
			}

			/// Construct using specified allocator.
			explicit use_future_t(const Allocator& allocator)
				: allocator_(allocator)
			{
			}

			/// Specify an alternate allocator.
			template <typename OtherAllocator>
			use_future_t<OtherAllocator> operator[](const OtherAllocator& allocator) const
			{
				return use_future_t<OtherAllocator>(allocator);
			}

			/// Obtain allocator.
			allocator_type get_allocator() const
			{
				return allocator_;
			}

		private:
			Allocator allocator_;
		};


		namespace detail
		{
			// Completion handler to adapt a promise as a completion handler.
			template <typename T>
			class promise_handler
			{
			public:
				// Construct from use_future special value.
				template <typename Alloc>
				promise_handler(use_future_t<Alloc> uf)
					: promise_(std::allocate_shared<GlacierLib::promise<T> >(
						typename std::allocator_traits<Alloc>::template rebind_alloc<char>(uf.get_allocator()),
						std::allocator_arg,
						typename std::allocator_traits<Alloc>::template rebind_alloc<char>(uf.get_allocator())))
				{

				}

				void operator()(T t)
				{
					promise_->set_value(t);
				}

				void operator()(const boost::system::error_code& ec, T t)
				{
					if (ec)
						promise_->set_exception(
							std::make_exception_ptr(
								boost::system::system_error(ec)));
					else
						promise_->set_value(t);
				}

				//private:
				std::shared_ptr<GlacierLib::promise<T> > promise_;
			};

			// Completion handler to adapt a void promise as a completion handler.
			template <>
			class promise_handler<void>
			{
			public:
				// Construct from use_future special value. Used during rebinding.
				template <typename Alloc>
				promise_handler(use_future_t<Alloc> uf)
					: promise_(std::allocate_shared<GlacierLib::promise<void> >(
						typename std::allocator_traits<Alloc>::template rebind_alloc<char>(uf.get_allocator()),
						std::allocator_arg,
						typename std::allocator_traits<Alloc>::template rebind_alloc<char>(uf.get_allocator())))
				{
				}

				void operator()()
				{
					promise_->set_value();
				}

				void operator()(const boost::system::error_code& ec)
				{
					if (ec)
						promise_->set_exception(
							std::make_exception_ptr(
								boost::system::system_error(ec)));
					else
						promise_->set_value();
				}

				//private:
				std::shared_ptr<GlacierLib::promise<void>> promise_;
			};

			// Ensure any exceptions thrown from the handler are propagated back to the
			// caller via the future.
			template <typename Function, typename T>
			void asio_handler_invoke(Function f, promise_handler<T>* h)
			{
				std::shared_ptr<GlacierLib::promise<T> > p(h->promise_);
				try
				{
					f();
				}
				catch (...)
				{
					p->set_exception(std::current_exception());
				}
			}

		} // namespace detail

		  // Handler traits specialisation for promise_handler.
		template <typename T>
		class async_result<detail::promise_handler<T> >
		{
		public:
			// The initiating function will return a future.
			typedef GlacierLib::future<T> type;

			// Constructor creates a new promise for the async operation, and obtains the
			// corresponding future.
			explicit async_result(detail::promise_handler<T>& h)
			{
				value_ = h.promise_->get_future();
			}

			// Obtain the future to be returned from the initiating function.
			type get() { return std::move(value_); }

		private:
			type value_;
		};

		// Handler type specialisation for use_future.
		template <typename Allocator, typename ReturnType>
		struct handler_type<use_future_t<Allocator>, ReturnType()>
		{
			typedef detail::promise_handler<void> type;
		};

		// Handler type specialisation for use_future.
		template <typename Allocator, typename ReturnType, typename Arg1>
		struct handler_type<use_future_t<Allocator>, ReturnType(Arg1)>
		{
			typedef detail::promise_handler<Arg1> type;
		};

		// Handler type specialisation for use_future.
		template <typename Allocator, typename ReturnType>
		struct handler_type<use_future_t<Allocator>,
			ReturnType(boost::system::error_code)>
		{
			typedef detail::promise_handler<void> type;
		};

		// Handler type specialisation for use_future.
		template <typename Allocator, typename ReturnType, typename Arg2>
		struct handler_type<use_future_t<Allocator>,
			ReturnType(boost::system::error_code, Arg2)>
		{
			typedef detail::promise_handler<Arg2> type;
		};
	}
}
namespace GlacierLib
{
	__declspec(selectany) boost::asio::use_future_t<> use_future;
}