#pragma once 
#include <GlacierLib\Future\FutureError.h>
#include <memory>
#include <atomic>
#include <condition_variable>
namespace GlacierLib
{
	template<typename T>
	class FutureImpl;

	template<typename T>
	class FutureImpl : std::enable_shared_from_this<FutureImpl<T>>
	{
	private:
		T result_;
		std::mutex mtx_; // result 锁
		std::condition_variable cv_; // 完成信号
		std::exception_ptr ec_ptr_; // 异常指针
		std::atomic_bool is_running_{ true };// 运算任务是否正在运行
		std::atomic_bool is_ready_{ false };// 结果是否准备完毕
		std::atomic_bool is_retrieved_{ false }; // 是否取回过结果
		std::atomic_bool has_stored_result_{ false }; // 结果是否已经储存
		std::function<void()> ready_callback_; // 设置回调
	private:
		using mutex_lock_t = std::unique_lock<std::mutex>;
	public:
		FutureImpl() = default;
		~FutureImpl() = default;

		FutureImpl(const FutureImpl& other) = delete;

		FutureImpl(FutureImpl&& other) noexcept
			: std::enable_shared_from_this<FutureImpl<T>>(std::move(other)),
			result_(std::move(other.result_)),
			mtx_(std::move(other.mtx_)),
			cv_(std::move(other.cv_)),
			ec_ptr_(std::move(other.ec_ptr_)),
			is_running_(std::move(other.is_running_)),
			is_ready_(std::move(other.is_ready_)),
			is_retrieved_(std::move(other.is_retrieved_)),
			has_stored_result_(std::move(other.has_stored_result_)),
			ready_callback_(std::move(other.ready_callback_))
		{
		}

		FutureImpl& operator=(const FutureImpl& other) = delete;

		FutureImpl& operator=(FutureImpl&& other) noexcept
		{
			if (this == &other)
				return *this;
			std::enable_shared_from_this<FutureImpl<T>>::operator =(std::move(other));
			result_ = std::move(other.result_);
			mtx_ = std::move(other.mtx_);
			cv_ = std::move(other.cv_);
			ec_ptr_ = std::move(other.ec_ptr_);
			is_running_ = std::move(other.is_running_);
			is_ready_ = std::move(other.is_ready_);
			is_retrieved_ = std::move(other.is_retrieved_);
			has_stored_result_ = std::move(other.has_stored_result_);
			ready_callback_ = std::move(other.ready_callback_);
			return *this;
		}

		bool is_ready() const
		{
			return is_ready_;
		}
		bool vaild() const noexcept
		{
			return !is_retrieved_;
		}
		void cancel()
		{
			mutex_lock_t lock(mtx_);
			if (!has_stored_result_)
			{
				set_exception(std::make_exception_ptr(future_error(future_errc::broken_promise)), {});
			}
		}
		bool has_exception() const noexcept
		{
			return ec_ptr_;
		}
		void set_ready_callback(std::function<void()> f)
		{
			ready_callback_ = std::move(f);
		}
		const std::function<void()>& get_ready_callback() const
		{
			return ready_callback_;
		}
	private:
		bool is_deferred() const
		{
			return !is_running_;
		}
		void wait(mutex_lock_t& lock)
		{
			cv_.wait(lock, [this] {return is_ready(); });
		}
	public:
		void wait()
		{
			mutex_lock_t lock(mtx_);
			wait(lock);
		}
		template<typename R, typename P>
		future_status wait_for(const std::chrono::duration<R, P>& rt)
		{
			if (is_deferred())
				return future_status::deferred;

			mutex_lock_t lock(mtx_);
			if (cv_.wait_for(lock, rt, [this] {return is_ready(); }))
				return future_status::ready;
			return future_status::timeout;
		}
		template<typename C, typename D>
		future_status wait_until(const std::chrono::time_point<C, D>& at)
		{
			if (is_deferred())
				return future_status::deferred;

			mutex_lock_t lock(mtx_);
			if (cv_.wait_until(lock, at, [this] {return is_ready(); }))
				return future_status::ready;
			return future_status::timeout;
		}
		template<typename T1>
		void set_value(T1&& val)
		{
			mutex_lock_t lock(mtx_);
			if (has_stored_result_)
				throw future_error(future_errc::promise_already_satisfied);

			result_ = static_cast<T>(std::forward<T1>(val));
			do_notify();
		}

		void set_exception(std::exception_ptr ptr)
		{
			mutex_lock_t lock(mtx_);
			set_exception(std::move(ptr), {});
		}
		T& get(bool get_only_once)
		{
			mutex_lock_t lock(mtx_);
			if (get_only_once)
			{
				if (is_retrieved_)
				{
					throw future_error(future_errc::future_already_retrieved);
				}
			}
			if (ec_ptr_)
			{
				std::rethrow_exception(ec_ptr_);
			}
			is_retrieved_ = true;
			wait(lock);
			if (ec_ptr_)
			{
				std::rethrow_exception(ec_ptr_);
			}
			return result_;
		}
		T& get_store_value()
		{
			return result_;
		}
		void do_notify()
		{
			has_stored_result_ = true;
			is_ready_ = true;
			if (ready_callback_)
			{
				ready_callback_();
				ready_callback_ = nullptr;
			}
			cv_.notify_all();
		}
	private:
		void set_exception(std::exception_ptr ptr, std::false_type)
		{
			if (has_stored_result_)
				throw future_error(future_errc::promise_already_satisfied);
			ec_ptr_ = std::move(ptr);
			do_notify();
		}
	};
	template<>
	class FutureImpl<void> : public FutureImpl<size_t>
	{
		using base_type = FutureImpl<size_t>;
	public:
		void set_value()
		{
			base_type::set_value(1);
		}

		void get(bool get_only_once)
		{
			base_type::get(get_only_once);
		}
	};
}