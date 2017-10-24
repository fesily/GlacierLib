#include "FutureExecatorInterface.h"
#include <GlacierLib\ThreadPool.h>
namespace GlacierLib
{
	class DefaultFutureThenExecator : public FutureExecatorInterface
	{
		void add_then(std::function<void()>&& fn) override
		{
			static v2::ThreadPool pool;
			pool.AddTask(std::move(fn));
		}
	};
	std::unique_ptr<FutureExecatorInterface>& get_execator_implementation()
	{
		static std::unique_ptr<FutureExecatorInterface> Instance(std::make_unique<detail::DefaultFutureThenExecator>());
		return Instance;
	}
	void set_future_execator(std::unique_ptr<FutureExecatorInterface> i)
	{
		std::swap(get_execator_implementation(), i);
	}

	FutureExecatorInterface* get_future_execator()
	{
		return get_execator_implementation().get();
	}
}