#include "ThreadPool.h"
namespace v1{

details::ThreadImpl::ThreadImpl(ThreadPool& pool, std::condition_variable& cv): pool_(pool),cv_(cv)
                                                   , thread_(&ThreadImpl::Run, this)
{
}

void details::ThreadImpl::Run()
{
	auto my = shared_from_this();
	while (!stop_)
	{
		const auto task(GetNextTask());
		if (!task)
		{
			if (!stop_)
				break;
			mutex_lock_t lock(mtx_);
			if (pool_.Steal(this))
				continue;
			cv_.wait(lock);
			continue;
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

details::Task details::ThreadImpl::GetNextTask()
{
	mutex_lock_t lock(mtx_);
	if (tasks_.empty())
		return nullptr;
	auto task(std::move(tasks_.front()));
	tasks_.pop_front();
	return task;
}
}