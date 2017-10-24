#pragma once 
#include <functional>
#include <memory>

namespace GlacierLib
{
	class FutureExecatorInterface
	{
	public:
		virtual ~FutureExecatorInterface() = default;
		virtual void add_then(std::function<void()>&& fn) = 0;
	};
	void set_future_execator(std::unique_ptr<FutureExecatorInterface> i);
	FutureExecatorInterface* get_future_execator();

	inline void add_then(std::function<void()>&& fn)
	{
		get_future_execator()->add_then(std::move(fn));
	}
}