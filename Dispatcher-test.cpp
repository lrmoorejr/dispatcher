/*
 * Copyright 2026 L. Richard Moore Jr.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <future>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "Dispatcher.hpp"

using namespace thr;

TEST_CASE( "Create / destroy dispatch" ) {
	Dispatcher* dispatch = new Dispatcher();
	auto threadCount = dispatch->getThreadCount();
	printf("Default thread count %d\n", threadCount);
	delete dispatch;
	CHECK(0 < threadCount);
}

TEST_CASE( "Set thread count" ) {
	Dispatcher* dispatch = new Dispatcher(7);
	auto threadCount = dispatch->getThreadCount();
	delete dispatch;
	CHECK(7 == threadCount);
}

TEST_CASE( "Unknown thread count" ) {
	Dispatcher* dispatch = new Dispatcher(0);
	auto threadCount = dispatch->getThreadCount();
	delete dispatch;
	CHECK(Dispatcher::defaultWorkerThreadCount);
}

TEST_CASE( "Simple dispatch" ) {
	Dispatcher* dispatch = new Dispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	dispatch->dispatch(1, [](Flattener<size_t>& flattener, size_t flatIndex){printf("Simple dispatch called\n");});
	delete dispatch;
}

TEST_CASE( "Simple dispatch, 3 count" ) {
	Dispatcher* dispatch = new Dispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(3, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(3 == count);
}

TEST_CASE( "Simple dispatch, 0 count" ) {
	Dispatcher* dispatch = new Dispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	CHECK_THROWS(dispatch->dispatch(0, [&count](Flattener<>& flattener, size_t flatIndex){ count++;}));
	delete dispatch;

	CHECK(0 == count);
}

TEST_CASE( "Simple dispatch, 0 on one dimension" ) {
	Dispatcher* dispatch = new Dispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	CHECK_THROWS(dispatch->dispatch({3,4,0,3,4}, [&count](Flattener<>& flattener, size_t flatIndex){ count++;}));
	delete dispatch;

	CHECK(0 == count);
}

TEST_CASE( "Simple dispatch, 4 threads, 4 count" ) {
	Dispatcher* dispatch = new Dispatcher(4);
	CHECK(4 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(4, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(4 == count);
}

TEST_CASE( "Simple dispatch, 2 threads, 100 count" ) {
	Dispatcher* dispatch = new Dispatcher(2);
	CHECK(2 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(100, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(100 == count);
}

TEST_CASE( "Simple dispatch, 100 threads, 2 count" ) {
	Dispatcher* dispatch = new Dispatcher(100);
	CHECK(100 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(2, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(2 == count);
}

TEST_CASE( "Weird shape (100000 total), 8 threads" ) {
	Dispatcher* dispatch = new Dispatcher(8);
	CHECK(8 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch({2, 5, 10, 100, 10}, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(100000 == count);
}

TEST_CASE( "Shape completedness test" ) {
	Dispatcher* dispatch = new Dispatcher(8);
	CHECK(8 == dispatch->getThreadCount());
	bool ran[2][5][10][100][10] = {false};
	dispatch->dispatch({2, 5, 10, 100, 10}, [&ran](Flattener<>& flattener, size_t flatIndex){
		int a = flattener.index(0, flatIndex);
		REQUIRE(a >= 0);
		REQUIRE(a < 2);
		int b = flattener.index(1, flatIndex);
		REQUIRE(b>= 0);
		REQUIRE(b< 5);
		int c = flattener.index(2, flatIndex);
		REQUIRE(c >= 0);
		REQUIRE(c < 10);
		int d = flattener.index(3, flatIndex);
		REQUIRE(d >= 0);
		REQUIRE(d < 100);
		int e = flattener.index(4, flatIndex);
		REQUIRE(e >= 0);
		REQUIRE(e < 10);
		ran[a][b][c][d][e] = true;
	});
	delete dispatch;

	for(int e = 0; e < 10; ++e) {
		for(int d = 0; d < 100; ++d) {
			for(int c = 0; c < 10; ++c) {
				for(int b = 0; b < 5; ++b) {
					for(int a = 0; a < 2; ++a) {
						CHECK(ran[a][b][c][d][e] == true);
					}
				}
			}
		}
	}
}

TEST_CASE( "Fast early termination" ) {
	Dispatcher* dispatch = new Dispatcher(100);

	std::atomic<int> count1(0);
	std::thread thread1([dispatch, &count1]() {
		dispatch->dispatch({2, 5, 100, 10}, [&count1](Flattener<>& flattener, size_t flatIndex){
			std::this_thread::sleep_for (std::chrono::milliseconds(10));
			count1++;
		});
	});

	// Terminate early
	delete dispatch;
	thread1.join();

	CHECK(10000 > count1);
}

TEST_CASE( "Early termination half way through" ) {
	Dispatcher* dispatch = new Dispatcher(100);

	std::atomic<int> count1(0);
	std::thread thread1([dispatch, &count1]() {
		dispatch->dispatch({2, 5, 100, 10}, [&count1](Flattener<>& flattener, size_t flatIndex){
			std::this_thread::sleep_for (std::chrono::milliseconds(10));
			count1++;
		});
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	// Terminate early
	dispatch->terminate();
	if(thread1.joinable())
		thread1.join();
	delete dispatch;

	CHECK(0 < count1);
	CHECK(10000 > count1);
}

TEST_CASE( "Repeated destroy-before-join stresses the terminate()/dispatch() race" ) {
	// terminate() (called from ~Dispatcher()) reads currentJob without holding jobMutex
	// (Dispatcher.hpp:88), and can return -- letting the Dispatcher be destroyed -- while
	// another thread is still inside dispatch() on it (Dispatcher.hpp:172-191). This is a
	// genuine data race / use-after-free, not just a theoretical one: under
	// -fsanitize=thread this loop reliably reproduces both a TSan race report pointing at
	// Dispatcher.hpp:88 vs Dispatcher.hpp:188, and (even without a sanitizer, occasionally)
	// a hard crash locking an already-destroyed mutex ("mutex lock failed: Invalid
	// argument"). A plain Debug/Release build without a sanitizer is unlikely to observe a
	// failure here -- this loop exists so the race has many chances to manifest when this
	// suite is run under ThreadSanitizer or AddressSanitizer, not as a reliable assertion
	// in an unsanitized run.
	for(int i = 0; i < 200; ++i) {
		Dispatcher* dispatch = new Dispatcher(8);
		std::atomic<int> count1(0);
		std::thread thread1([dispatch, &count1]() {
			dispatch->dispatch({2, 5, 20, 10}, [&count1](Flattener<>& flattener, size_t flatIndex){
				count1++;
			});
		});

		// Terminate early, before thread1 necessarily even started dispatch()
		delete dispatch;
		thread1.join();
	}
}

TEST_CASE( "Concurrent dispatch() calls do not silently corrupt state in a release (NDEBUG) build" ) {
	// The only thing preventing two concurrent dispatch() calls on the same instance from
	// corrupting `currentJob` is `ensure(currentJob == nullptr, ...)` (Dispatcher.hpp:171),
	// which compiles to a no-op with the condition unevaluated under NDEBUG. This test
	// documents the desired behavior (concurrent dispatch() must not silently hang/corrupt
	// state) using a bounded wait rather than an unbounded join, so a regression here fails
	// the test instead of hanging the suite. It only exercises the current build's actual
	// NDEBUG-ness (Debug builds have ensure() active and would instead abort the whole test
	// process on this scenario, which Catch2 can't catch as a normal test failure -- see
	// Dispatcher-ndebug-test.cpp for the dedicated NDEBUG build of this same scenario).
#ifdef NDEBUG
	// Heap-allocate and deliberately leak on the buggy path: threadA can be permanently
	// stuck inside dispatch() on this instance, so destroying it out from under that thread
	// would trigger the separate destructor race covered by the test above.
	Dispatcher* dispatch = new Dispatcher(8);

	std::promise<void> aDone;
	auto aDoneFuture = aDone.get_future();
	std::thread threadA([dispatch, &aDone]() {
		dispatch->dispatch({2, 5, 200, 10}, [](Flattener<>& flattener, size_t flatIndex){
			std::this_thread::sleep_for(std::chrono::microseconds(50));
		});
		aDone.set_value();
	});

	// Give threadA a chance to actually enter dispatch() and set currentJob before threadB does.
	std::this_thread::sleep_for(std::chrono::milliseconds(20));

	std::thread threadB([dispatch]() {
		dispatch->dispatch({4}, [](Flattener<>& flattener, size_t flatIndex){});
	});
	threadB.join();

	auto status = aDoneFuture.wait_for(std::chrono::seconds(5));
	if(status == std::future_status::timeout) {
		threadA.detach();
		FAIL("threadA's dispatch() call never completed -- a concurrent dispatch() call "
		     "silently overwrote currentJob and orphaned threadA's job (leak + permanent hang)");
	} else {
		threadA.join();
		delete dispatch;
	}
#else
	SUCCEED("built without NDEBUG -- see Dispatcher-ndebug-test.cpp for this scenario under NDEBUG");
#endif
}

TEST_CASE( "dispatch() after terminate() silently does no work instead of erroring" ) {
	// Once terminate() has run, terminateAll is permanently true and the worker pool is
	// gone. A later dispatch() call still passes the ensure() guard and sets currentJob,
	// but its wait predicate (job->complete() || terminateAll) is already satisfied by
	// terminateAll alone, so it returns almost immediately without ever invoking the
	// worker -- silently, with no exception or other signal to the caller. This documents
	// that behavior; it's a design/robustness gap, not a memory-safety bug.
	Dispatcher dispatch(4);
	dispatch.terminate();

	std::atomic<int> count(0);
	dispatch.dispatch(100, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});

	CHECK(0 == count);
}
