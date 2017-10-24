#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <algorithm>
#include <vector>
#include <deque>
#include <unordered_set>
#include <queue>
#include <set>
#include <unordered_map>
#include <iostream>

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
				/*if (noTaskCv_.empty())
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
#include <tbb/queuing_rw_mutex.h>
#include <tbb/queuing_mutex.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_priority_queue.h>

/*
 * v2 版本,可以 自伸缩的 负载均衡的 线程池
 * 
 * 问题:不能识别长时间任务,因而智能伸缩线程池
 *	   工作对象不能转移,没有做到高内聚
 */
namespace v2
{
	constexpr size_t defaultQueueSize = 1000;
	constexpr size_t createWorkerQueueSizelimit = 10000;
	class ThreadPool;
	using Task = std::function<void()>;
	using TimerTask = std::function<bool()>;
	namespace detail
	{
		struct NullLock
		{
			void lock() {}
			void unlock() {}
		};
		class Worker : public std::enable_shared_from_this<Worker>
		{
			friend ThreadPool;
			using base_type = std::thread;
		public:
			explicit Worker(ThreadPool& worker_pool, size_t defaultQueueSize = defaultQueueSize)
				: workerPool_(worker_pool), thread_([this]() {this->Run(); })
			{
				taskQueue_.set_capacity(defaultQueueSize);
			}
			~Worker()
			{
				if (!taskQueue_.empty())
				{
					assert(false);
				}
			}
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
			void Stop()
			{
				stop_ = true;
				cv_.notify_all();
			}
		protected:
			void Run();
		protected:
			ThreadPool& workerPool_;
			std::condition_variable_any cv_;
			std::atomic_bool stop_{ false };
			std::atomic_bool isWaitTask_{ false };
			tbb::concurrent_bounded_queue<Task> taskQueue_;
			std::thread thread_;
		};
		class TimerWorker
		{
			friend ThreadPool;
			using base_type = std::thread;

			using clock = std::chrono::system_clock;
			using time_point = clock::time_point;
			using duration = clock::duration;
		public:
			using Handle = void*;
		private:
			struct TimerTaskImpl : std::enable_shared_from_this<TimerTaskImpl>
			{
				static constexpr size_t defaultLiveRefCount = 3;
				struct less
				{
					bool operator()(const std::shared_ptr<TimerTaskImpl>& l , const std::shared_ptr<TimerTaskImpl>& r) const
					{
						return l->point < r->point;
					}
				};

				TimerTaskImpl(const time_point& point, const TimerTask& task, const duration& delay)
					: point(point),
					task(task),
					delay(delay),
					inTimerQueue(false),
					stop(false)
				{
				}

				Handle GetHandle() const
				{
					return const_cast<TimerTaskImpl*>(shared_from_this().get());
				}

				bool UpdateNextTimer()
				{
					if (stop || delay == duration())
					{
						return false;
					}
					point += delay;
					return true;
				}
				time_point point;
				TimerTask task;
				duration delay;
				std::atomic_size_t count{0};
				std::atomic_bool inTimerQueue;
				std::atomic_bool stop;
			};
		public:
			TimerWorker()
				:  thread_([this]() {this->Run(); })
			{

			}

		private:
			static time_point GetNextTimePoint(duration delay)
			{
				return clock::now() + delay;
			}
			Handle AddTask(std::shared_ptr<TimerTaskImpl>&& task)
			{
				const auto handle = task->GetHandle();
				{
					decltype(memoryMtx_)::scoped_lock lock(memoryMtx_, true);
					if (!memoryTimers_.emplace(handle, task).second)
						return nullptr;
				}
				decltype(mtx_)::scoped_lock lock(mtx_);
				task->inTimerQueue = true;
				timerQueue_.push_back(std::move(task));
				std::push_heap(timerQueue_.begin(), timerQueue_.end(), TimerTaskImpl::less());
				cv_.notify_all();
				return handle;
			}
		public:
			Handle AddTask(TimerTask&& task, time_point point, duration delay)
			{
				return AddTask(std::make_shared<TimerTaskImpl>(point, std::move(task), delay));
			}

			Handle AddOnceTask(Task&& task, time_point point)
			{
				return AddTask([t = std::move(task)] {
					t();
					return false;
				} , point, duration());
			}
			Handle AddOnceTask(Task&& task, duration delay)
			{
				return AddOnceTask(std::move(task), GetNextTimePoint(delay));
			}

			Handle AddPeriodTask(Task&& task, duration delay)
			{
				return AddTask([t = std::move(task)]{
					t();
					return true;
				}, GetNextTimePoint(delay), delay);
			}

			Handle AddPeriodTask(TimerTask&& task, duration delay)
			{
				return AddTask(std::move(task), GetNextTimePoint(delay), delay);
			}
			void RemoveTask(Handle handle)
			{
				decltype(memoryMtx_)::scoped_lock lock(memoryMtx_, false);
				const auto iter = memoryTimers_.find(handle);
				if (iter != memoryTimers_.end())
				{
					const auto task = iter->second;
					lock.upgrade_to_writer();
					memoryTimers_.erase(iter);
					lock.release();
					task->stop = true;
					if (task->inTimerQueue)
					{
						decltype(mtx_)::scoped_lock lock1(mtx_);
						const auto iter1 = std::find_if(timerQueue_.cbegin(), timerQueue_.cend(), [handle](const std::shared_ptr<TimerTaskImpl>& t) {
							return handle == t->GetHandle();
						});
						if (iter1 != timerQueue_.cend())
						{
							timerQueue_.erase(iter1);
						}
					}
				}
				
			}
			void WaitAllTimer()
			{
				while (true)
				{
					using namespace std::chrono_literals;
					std::this_thread::sleep_for(10ms);
					if (memoryTimers_.empty())
						return;
				}
			}
			void Stop()
			{
				stop_ = true;
				cv_.notify_all();
				if (thread_.joinable())
				{
					thread_.join();
				}
			}
		protected:
			void Run()
			{
				struct WaitLock
				{
					WaitLock(decltype(mtx_)::scoped_lock& l, decltype(mtx_)& m)
						:lock_(l)
						, mtx(m)
					{
					}
					void lock()
					{
					}
					void unlock()
					{
						lock_.release();
					}
					decltype(mtx_)::scoped_lock& lock_;
					decltype(mtx_)& mtx;
				};
				
				while (!stop_)
				{
					const auto now = std::chrono::system_clock::now();
					decltype(mtx_)::scoped_lock lock(mtx_);
					if (timerQueue_.empty())
					{//空队列，等待
						WaitLock n(lock, mtx_);
						cv_.wait(n);
						continue;
					}
					// 查找最小堆中最小时间的定时器
					std::pop_heap(timerQueue_.begin(), timerQueue_.end(), TimerTaskImpl::less());
					auto task = timerQueue_.back();

					if (now > task->point)
					{
						// 出队列，准备调用
						timerQueue_.pop_back();
						task->inTimerQueue = false;
						const auto handle = task->GetHandle();
						lock.release();

						if (!task->stop)
						{
							const auto& t = task->task;
							assert(t);
							try
							{
								if (t())
								{
									if (task->UpdateNextTimer())
									{
										//如果能继续的话，重新加入队列
										task->inTimerQueue = true;
										lock.acquire(mtx_);
										timerQueue_.push_back(task);
										std::push_heap(timerQueue_.begin(), timerQueue_.end(), TimerTaskImpl::less());
										continue;
									}
								}
							}
							catch (...)
							{

							}
						}
						RemoveTask(handle);
					}
					else
					{
						WaitLock n(lock, mtx_);
						cv_.wait_until(n, task->point);
					}
				}
			}
		protected:
			std::condition_variable_any cv_;
			std::atomic_bool stop_{ false };

			tbb::queuing_mutex mtx_;
			std::vector<std::shared_ptr<TimerTaskImpl>> timerQueue_;
			tbb::queuing_rw_mutex memoryMtx_;
			std::unordered_map<Handle,std::shared_ptr<TimerTaskImpl>> memoryTimers_;
			std::thread thread_;
		};
		inline bool operator==(const std::weak_ptr<Worker>& l, const std::weak_ptr<Worker>& r)
		{
			return l.lock() == r.lock();
		}
	}
	class ThreadPool
	{
		friend detail::Worker;
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

		tbb::concurrent_queue<Task> tasksQueue_;

		std::condition_variable_any noTaskCv_;
		std::atomic_int32_t sleepWorkerSize_{0};
		tbb::queuing_rw_mutex workerMtx_;
		std::unordered_set<std::shared_ptr<detail::Worker>> workers_;
		std::shared_ptr<detail::TimerWorker> timer_;
	public:
		static size_t DefaultThreadSize(Type t)
		{
			const auto cpu = std::thread::hardware_concurrency();
			switch (t) {
			case Type::CPU:
				return cpu * 1;
			default:
				return cpu * 2;
			}
		}
	public:
		ThreadPool(uint8_t coreThreadSize = 0, uint8_t maxThreadSize = 0, size_t keepAliveTime = 0, strategy  s = strategy::delay, Type t = Type::All)
			:maxThreadSize_(maxThreadSize), coreThreadSize_(coreThreadSize), keepAliveTime_(keepAliveTime)
		{
			if (coreThreadSize_ == 0)
			{
				coreThreadSize_ = std::thread::hardware_concurrency();
			}
			if (maxThreadSize_ == 0)
			{
				maxThreadSize_ = DefaultThreadSize(t);
			}
			if (keepAliveTime_ == 0)
			{
				keepAliveTime_ = 30;
			}
			if (s == strategy::immediately)
			{
				for (int i = 0; i < coreThreadSize_; ++i)
				{
					AddWorker(std::make_shared<detail::Worker>(*this));
				}
			}
		}

		~ThreadPool()
		{
			DestoryPool();
		}
		void DestoryPool()
		{
			if (timer_)
			{
				timer_->Stop();
			}
			decltype(workerMtx_)::scoped_lock lock(workerMtx_, true);
			for (auto element : workers_)
				element->Stop();
			for (auto element : workers_)
			{
				if (element->thread_.joinable())
					element->thread_.join();
			}
		}
	private:
		void AddTaskByCreateWorker(Task&& task)
		{
			auto worker = std::make_shared<detail::Worker>(*this);
			AddWorker(worker);
			worker->AddTask(std::move(task));
		}
		void AddTaskByCreateTimerWorker()
		{
			auto worker = std::make_shared<detail::Worker>(*this);
			AddWorker(worker);

			if (!timer_)
			{
				timer_.reset(new detail::TimerWorker);
			}

			TimerTask fn = [this, worker]{
				if (worker->taskQueue_.empty() || worker->stop_)
				{
					worker->Stop();
					DestoryWorker(worker);
					return false;
				}
				return true;
			};
			timer_->AddPeriodTask(fn, std::chrono::seconds(keepAliveTime_));
		}
		bool RandPushTaskToWorker(const Task& task)
		{
			// 如果有休眠的 优先唤醒休眠的线程
			if (sleepWorkerSize_ > 0)
			{
				if (RandFind([task](const std::shared_ptr<detail::Worker>& element) {
					if (element->isWaitTask_)
					{
						return element->TryAddTask(task);
					}
					return false;
				}))
					return true;
			}
			return RandFind([task](const std::shared_ptr<detail::Worker>& element)
			{
				return element->TryAddTask(task);
			});
		}
		bool WeakRandomWoker()
		{
			if (sleepWorkerSize_ == 0)
			{
				return false;
			}
			return RandFind([](const std::shared_ptr<detail::Worker>& element)
			{
				if (element->isWaitTask_)
				{
					element->cv_.notify_all();
					return true;
				}
				return false;
			});
		}
		void WeakAllWorker()
		{
			decltype(workerMtx_)::scoped_lock lock(workerMtx_, false);
			for (auto& element : workers_)
			{
				auto worker = element.get();
				worker->cv_.notify_all();
			}
		}
	public:
		void AddTask(Task&& task)
		{
			if (workers_.size() < coreThreadSize_)
			{
				AddTaskByCreateWorker(std::move(task));
			}
			else if (workers_.size() >= coreThreadSize_ && workers_.size() < maxThreadSize_)
			{
				if (RandPushTaskToWorker(task)) return;

				tasksQueue_.push(task);

				if (WeakRandomWoker()) return;

				if (tasksQueue_.unsafe_size() > 1000)
				{
					AddTaskByCreateTimerWorker();
				}
			}
			else
			{
				if (RandPushTaskToWorker(task)) return;
				tasksQueue_.push(task);
			}
		}
		void Wait()
		{
			detail::NullLock lock;
			noTaskCv_.wait(lock, [this]()
			{
				if (!tasksQueue_.empty())
				{
					WeakAllWorker();
					return false;
				}
				else
				{
					bool ret = true;
					decltype(workerMtx_)::scoped_lock lock(workerMtx_, false);
					for (auto& element : workers_)
					{
						auto worker = element.get();
						if (!worker->taskQueue_.empty())
						{
							worker->cv_.notify_all();
							ret = false;
						}
					}
					return ret;
				}
				return true;
			});
		}
		void WaitTimer()
		{
			if (!timer_)
			{
				return;
			}
			while (true)
			{
				if (timer_->timerQueue_.empty())
				{
					decltype(timer_->memoryMtx_)::scoped_lock lock(timer_->memoryMtx_);
					if (timer_->memoryTimers_.empty())
						return;
				}
				using namespace std;
				std::this_thread::sleep_for(10ms);
			}
		}
	private:
		template<typename Fn>
		bool RandFind(Fn&& fn)
		{
			decltype(workerMtx_)::scoped_lock lock;
			// 尝试取得锁
			auto try_count = 10;
			while (!lock.try_acquire(workerMtx_, false))
			{
				if (--try_count)
					return false;
			}
			if (workers_.empty())
				return false;

			const auto threadsEnd = workers_.end();;
			const auto threadSize = workers_.size();;
			const auto beginIndex = std::rand() % threadSize;

			auto begin = workers_.begin();
			std::advance(begin, beginIndex);
			if (std::find_if(begin, threadsEnd, fn) != threadsEnd)
				return true;
			auto rend = std::make_reverse_iterator(workers_.begin());
			if (std::find_if(std::make_reverse_iterator(begin),
				rend, fn) != rend)
				return true;
			return false;
		}
	private:
		bool TryGetTask(Task& task)
		{
			if (tasksQueue_.try_pop(task))
			{
				if (tasksQueue_.empty())
					noTaskCv_.notify_all();
				return true;
			}
			// 窃取任务
			const bool ret = RandFind([&task](const std::shared_ptr<detail::Worker>& element)
			{
				auto worker = element.get();
				if (!worker->taskQueue_.empty())
				{
					return worker->taskQueue_.try_pop(task);
				}
				return false;
			});
			if (!ret)
			{
				noTaskCv_.notify_all();
			}
			return ret;
		}
	private:
		void AddWorker(std::shared_ptr<detail::Worker> worker)
		{
			decltype(workerMtx_)::scoped_lock lock(workerMtx_, true);
			workers_.insert(std::move(worker));
		}
		void DestoryWorker(std::shared_ptr<detail::Worker> worker)
		{
			{
				decltype(workerMtx_)::scoped_lock lock(workerMtx_, true);
				workers_.erase(worker);
			}
			if (worker->thread_.joinable())
				worker->thread_.join();
		}
	};

	inline void detail::Worker::Run()
	{
		struct FlagLock
		{
			explicit FlagLock(Worker& worker)
				: worker(worker)
			{
			}
			void lock()
			{
				worker.isWaitTask_ = false;
				--worker.workerPool_.sleepWorkerSize_;
			}
			void unlock()
			{
				worker.isWaitTask_ = true;
				++worker.workerPool_.sleepWorkerSize_;
			}
			Worker& worker;
		};
		while (!stop_)
		{
			Task task;
			if (!taskQueue_.try_pop(task))
			{
				if (!workerPool_.TryGetTask(task))
				{
					FlagLock lock(*this);
					cv_.wait(lock);
					continue;
				}
			}
			assert(task);
			try
			{
				task();
			}
			catch (...)
			{

			}
		}
		// 如果有未完成任务,则返回给列表
		if (!taskQueue_.empty())
		{
			Task task;
			while (taskQueue_.try_pop(task))
				workerPool_.tasksQueue_.push(task);
		}
	}
}