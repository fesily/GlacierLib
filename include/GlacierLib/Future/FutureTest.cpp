#include <GlacierLib\Future\Future.h>
#include <iostream>
#if 1
namespace GlacierLib
{
	struct A {};
	struct B : A {};
	struct C
	{
		operator B() { return {}; }
	};

	void TestPackagedTask()
	{
		auto task = GlacierLib::make_packaged_task([]() {return 1.1; });
		auto f111 = task.get_future();
		assert(task.valid());
		task();
		assert(task.valid());
		auto v = f111.get();
		assert(!task.valid());
	}
	void TestDetail()
	{
		static_assert(GlacierLib::detail::is_convertible<B*, A*>());
		static_assert(GlacierLib::detail::is_convertible<B&, A&>());
		static_assert(GlacierLib::detail::is_convertible<C, A>());
		static_assert(GlacierLib::detail::is_convertible<C, A&&>());
	}
	void TestFuture()
	{
		GlacierLib::promise<int> promise;
		GlacierLib::future<int> future = promise.get_future();
		auto fn = [](int i)
		{
			std::cout << "then 1" << std::endl;
			return 1.1;
		};
		auto fn1 = [](GlacierLib::future<double>& future)
		{
			std::cout << "then true" << std::endl;
			return 1.1;
		};

		auto f1 = future.then(fn).then(fn1)
			.then([](GlacierLib::future<double>&&) {return 1.1; })
			.then([](double) {return 1.1; })
			.then([](double&&) {throw std::exception("exception test!"); return 1.1; })
			.then([](double&&) {return A{}; })
			.then([](A) {return A{}; })
			.then([](A&) {return A{}; })
			.then([](A&&) {return B{}; })
			.then([](A) {return B{}; })
			.then([](A&) {return B{}; })
			.then([](A&&) {return A{}; })
			.then([](GlacierLib::future<A>& future)
		{
			try
			{
				future.get();
			}
			catch (std::exception& e)
			{
				std::cout << e.what();
			}
			return A{};
		})
			.then([]()
		{
			std::cout << "then closed" << std::endl;
		});
		promise.set_value(1);
		f1.get();
	}
	void TestWhenAny()
	{
		promise<void> p1;
		promise<int> p2;
		promise<float> p3;
		promise<bool> p4;

		auto ft = when_any(p1.get_future(), p2.get_future(), p3.get_future(), p4.get_future());
		assert(!ft.is_ready());
		p1.set_value();
		Sleep(100);
		assert(ft.is_ready());
		p2.set_value(1);
		Sleep(100);
		assert(ft.is_ready());
		p3.set_value(1.1);
		Sleep(100);
		assert(ft.is_ready());
		p4.set_value(false);
		Sleep(100);
		assert(ft.is_ready());
		auto tuple = ft.get();
	}
	void TestWhenAll()
	{
		promise<void> p1;
		promise<int> p2;
		promise<float> p3;
		promise<bool> p4;

		auto ft = when_all(p1.get_future(), p2.get_future(), p3.get_future(), p4.get_future());
		assert(!ft.is_ready());
		p1.set_value();
		Sleep(100);
		assert(!ft.is_ready());
		p2.set_value(1);
		Sleep(100);
		assert(!ft.is_ready());
		p3.set_value(1.1);
		Sleep(100);
		assert(!ft.is_ready());
		p4.set_value(false);
		Sleep(100);
		assert(ft.is_ready());
		auto tuple = ft.get();
	}
	void Test()
	{
		TestFuture();
		TestDetail();
		TestPackagedTask();
		TestWhenAny();
		TestWhenAll();
	}
}
#endif