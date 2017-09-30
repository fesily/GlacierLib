#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <algorithm>
#include <vector>
#include <deque>
#include <tbb/concurrent_vector.h>

namespace v1
{
	
	class ThreadPool;
	namespace details
	{
		class ThreadImpl;
		using mutex_lock_t = std::unique_lock<std::mutex>;
		using Task = std::function<void()>;
		/*
		 * 主动拉去任务, 没有任务窃取任务
		 */
		class ThreadImpl : std::enable_shared_from_this<ThreadImpl>
		{
			friend ThreadPool;
		public:
			explicit ThreadImpl(ThreadPool& pool, std::condition_variable& cv);
			virtual ~ThreadImpl() = default;

			void Run();

			void AddTask(Task&& task)
			{
				mutex_lock_t lock(mtx_);
				tasks_.push_back(std::move(task));
			}

			virtual Task GetNextTask();
			void Stop()
			{
				stop_ = true;
				thread_.detach();
			}
		protected:
			ThreadPool& pool_;
			std::condition_variable& cv_;
		private:
			std::deque<Task> tasks_; // 待处理任务队列
			std::mutex mtx_;
			std::atomic_bool stop_{ false };
			std::thread thread_;

		};
		/*
		 * 定时自毁的线程:问题,没有向pool申请销毁
		 */
		class DynamicThreadImpl : public ThreadImpl
		{
			friend ThreadPool;

		public:
			DynamicThreadImpl(ThreadPool& pool, std::condition_variable& cv,
				const std::chrono::time_point<std::chrono::system_clock>& destory_time)
				: ThreadImpl(pool, cv),
				destoryTime(destory_time)
			{
			}

			Task GetNextTask() override
			{
				auto task = ThreadImpl::GetNextTask();
				if (!task && std::chrono::system_clock::now() >= destoryTime)
				{
					Stop();
					return nullptr;
				}
				return task;
			}
		private:
			std::chrono::time_point<std::chrono::system_clock> destoryTime;
		};
	}

	/**
	 * \brief 自增长线程池,问题: 锁的问题太多了
	 */
	class ThreadPool
	{
		using mutex_lock_t = std::unique_lock<std::mutex>;
	public:
		enum class strategy
		{
			immediately,
			delay,
		};
	private:
		std::atomic_uint8_t coreThreadSize_; // 核心线程数量
		std::atomic_uint8_t maxThreadSize_; // 最大线程数量
		std::atomic_size_t keepAliveTime_; // 动态线程存活时间

		std::mutex threadMtx_;
		std::mutex taskMtx_;
		std::condition_variable cv_;

		std::vector<details::Task> tasks_; // 待处理任务队列
		std::atomic_size_t taskSize_{ 0 };

		std::vector<std::shared_ptr<details::ThreadImpl>> threads_;
	public:
		ThreadPool(uint8_t coreThreadSize = DefaultThreadSize(), uint8_t maxThreadSize = DefaultThreadSize(), size_t keepAliveTime = 3600, strategy  s = strategy::delay)
			:coreThreadSize_(coreThreadSize), maxThreadSize_(maxThreadSize), keepAliveTime_(keepAliveTime)
		{
			if (s == strategy::immediately)
				CreateThread(std::thread::hardware_concurrency());
		}

		~ThreadPool()
		{
			DestoryThread(threads_.size());
		}
		static size_t DefaultThreadSize()
		{
			static size_t size = std::thread::hardware_concurrency() * 2 + 1;
			return size;
		}
	public:
		void AddTask(details::Task&& task)
		{
			{
				mutex_lock_t lock(taskMtx_);
				tasks_.push_back(std::forward<details::Task>(task));
				++taskSize_;
				/*if (cv_.empty())
				{
				AutoThreadResize();
				}
				else*/
				{
					cv_.notify_one();
				}
			}
		}
		// 线程自增长处理
		void AutoThreadResize()
		{
			mutex_lock_t lock(threadMtx_);
			const auto size = threads_.size();
			if (size < coreThreadSize_)
			{
				CreateThread(1);
			}
			else if (size >= coreThreadSize_ && size < maxThreadSize_)
			{
				CreateKeepAliveThread(1);
			}
		}
	private:
		void Resize(uint8_t sz)
		{
			const size_t now = threads_.size();
			const int64_t changed = sz - now;
			if (changed > 0)
			{
				CreateThread(changed);
			}
			else if (changed < 0)
			{
				DestoryThread(changed);
			}
		}
	private:
		void CreateThread(size_t count)
		{
			while (count--)
				threads_.push_back(std::make_shared<details::ThreadImpl>(*this, cv_));
		}
		void CreateKeepAliveThread(size_t count)
		{
			auto timePoint = std::chrono::system_clock::from_time_t(
				std::chrono::system_clock::now().time_since_epoch().count() + keepAliveTime_.load());
			while (count--)
			{
				threads_.push_back(
					std::make_shared<details::DynamicThreadImpl>(*this, cv_, timePoint));
			}
		}
		void DestoryThread(size_t count)
		{
			count = std::min(threads_.size(), count);
			while (count--)
			{
				auto& thr = threads_.back();
				thr->Stop();
				threads_.pop_back();
			}
			// 唤醒所有线程
			cv_.notify_all();
		}
		template<typename Fn>
		bool RandFind(Fn&& fn)
		{
			mutex_lock_t lock(threadMtx_, std::try_to_lock);
			if (!lock.owns_lock())
				return false;

			const auto threadsEnd = threads_.end();;
			const auto threadSize = threads_.size();;
			const auto beginIndex = std::rand() % threadSize;

			auto begin = threads_.begin();
			std::advance(begin, beginIndex);
			if (std::find_if(begin, threadsEnd, fn) != threadsEnd)
				return true;
			if (std::find_if(std::make_reverse_iterator(begin),
				threads_.rend(), fn) != threads_.rend())
				return true;
			return false;
		}
	public:
		bool Steal(details::ThreadImpl* thr)
		{
			if (taskSize_ > 0)
			{
				mutex_lock_t lock(taskMtx_, std::try_to_lock);
				if (lock.owns_lock())
				{
					std::move(tasks_.begin(), tasks_.end(), std::back_inserter(thr->tasks_));
					taskSize_ = 0;
					return true;
				}
			}
			const auto fn = [thr, this](decltype(threads_)::value_type& element)
			{
				auto th = element.get();
				if (th == thr) return false;

				mutex_lock_t lock(th->mtx_, std::try_to_lock);
				if (!lock.owns_lock()) return false;

				auto sz = th->tasks_.size();
				sz /= 3;
				if (sz > 0)
				{
					while (sz--)
					{
						thr->tasks_.push_back(std::move(th->tasks_.back()));
						th->tasks_.pop_back();
					}
					return true;
				}
				return false;
			};
			return RandFind(fn);
		}
	};

}
#include <assert.h>
#include <condition_variable>
#include <tbb/spin_rw_mutex.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_set.h>

namespace v2
{
	class ThreadPool;
	using Task = std::function<void()>;
	using TimerTask = std::function<bool()>;
	namespace detail
	{
		class Worker : public std::thread
		{
			friend ThreadPool;
			using base_type = std::thread;
			struct NullLock
			{
				NullLock(std::atomic_bool& b)
					:flag_(b)
				{

				}
				void lock()
				{
					flag_ = true;
				}
				void unlock()
				{
					flag_ = false;
				}
				std::atomic_bool& flag_;
			};
		public:
			using task_container = tbb::concurrent_bounded_queue<Task>;
			void AddTask(Task&& task)
			{
				taskQueue_.push(std::move(task));
				cv_.notify_all();
			}
			bool TryAddTask(const Task& task)
			{
				if (taskQueue_.try_push(task))
				{
					cv_.notify_all();
					return true;
				}
				return false;
			}
			void AddTimerTask(TimerTask&& task)
			{
				AddTask([task, this]() mutable
				{
					if (!task())
						this->AddTimerTask(std::move(task));
				});
			}
		protected:
			Task GetNextTask()
			{
				Task task;
				taskQueue_.pop(task);
				return task;
			}
			bool TryGetNextTask(Task& task)
			{
				return taskQueue_.try_pop(task);
			}
			void Run()
			{
				while (!stop_)
				{
					Task task;
					if (!taskQueue_.try_pop(task))
					{
						if (!(workerPool_ && workerPool_->TryGetNextTask(task)))
						{
							NullLock lock(isSleep_);
							cv_.wait(lock);
							continue;
						}
					}
					try
					{
						task();
					}
					catch (...)
					{
					}
				}
			}
		protected:
			Worker* workerPool_;
			std::condition_variable_any cv_;
			std::atomic_bool stop_{ false };
			std::atomic_bool isSleep_{ false };
			task_container taskQueue_;
		};
	}
}

namespace v2
{
	class ThreadPool : detail::Worker
	{

	public:
		enum class strategy
		{
			immediately,
			delay,
		};
		enum class Type
		{
			IO,
			CPU,
			All,
		};
	private:
		std::atomic_size_t maxThreadSize_; // 最大线程数量
		std::atomic_size_t coreThreadSize_; // 核心线程数量
		std::atomic_size_t keepAliveTime_; // 动态线程存活时间

		tbb::spin_rw_mutex mtx_;
		tbb::concurrent_unordered_set<std::shared_ptr<detail::Worker>> workers_;
	public:
		static size_t DefaultThreadSize(Type t)
		{
			const auto cpu = std::thread::hardware_concurrency();
			switch (t) {
			case Type::CPU: 
				return cpu * 1 + 1;
			default: 
				return cpu * 2 + 1;
			}
		}
	public:
		ThreadPool(uint8_t coreThreadSize = 0, uint8_t maxThreadSize = 0, size_t keepAliveTime = 0, strategy  s = strategy::delay,Type t = Type::All)
			:maxThreadSize_(maxThreadSize), coreThreadSize_(coreThreadSize), keepAliveTime_(keepAliveTime)
		{
			if (coreThreadSize_ == 0)
			{
				coreThreadSize_ = thread::hardware_concurrency();
			}
			if (maxThreadSize_ == 0)
			{
				maxThreadSize_ = DefaultThreadSize(t);
			}
			if (keepAliveTime_ == 0)
			{
				keepAliveTime_ = 1800;
			}
			
			if (s == strategy::immediately)
			{
				//CreateThread(std::thread::hardware_concurrency());
			}
		}

		~ThreadPool()
		{
			//DestoryThread(threads_.size());
		}

		void AddTaskByCreateWorker(Task&& task)
		{
			auto worker = std::make_shared<detail::Worker>();
			AddWorker(worker);
			worker->AddTask(std::move(task));
		}
		void AddTaskByCreateTimerWorker(Task&& task)
		{
			auto worker = std::make_shared<detail::Worker>();
			AddWorker(worker);
			worker->AddTask(std::move(task));
			auto fn = [this, 
				timePoint = std::chrono::system_clock::from_time_t(std::chrono::system_clock::now().time_since_epoch().count() + keepAliveTime_),
				worker]{
				if (std::chrono::system_clock::now() >= timePoint && worker->taskQueue_.empty())
				{
					worker->stop_ = true;
					return true;
				}
				return false;
			};
			worker->AddTimerTask(fn);
		}
		void AddTask(Task&& task)
		{
			if (workers_.size() < coreThreadSize_)
			{
				AddTaskByCreateWorker(std::move(task));
			}
			else if (workers_.size() >= coreThreadSize_ && workers_.size() < maxThreadSize_)
			{
				if (TryAddTask(task))
				{
					
				}
				else
				{
					AddTaskByCreateTimerWorker(std::move(task));
				}
			}
			else
			{
				{
					decltype(mtx_)::scoped_lock lock(mtx_, false);
					for (auto iter{ workers_.begin() }, end{ workers_.end() }; iter != end; ++iter)
					{
						auto element = *iter;
						if (element && element->TryAddTask(task))
							return;
					}
				}
				AddTaskByCreateTimerWorker(std::move(task));
			}
		}
	private:
		void AddWorker(std::shared_ptr<detail::Worker> worker)
		{
			decltype(mtx_)::scoped_lock lock(mtx_, false);
			workers_.insert(std::move(worker));
		}
		void DestoryWorker(std::shared_ptr<detail::Worker> worker)
		{
			decltype(mtx_)::scoped_lock lock(mtx_, true);
			workers_.unsafe_erase(worker);
		}
	};
}
