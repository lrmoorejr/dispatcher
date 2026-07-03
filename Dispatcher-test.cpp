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
		try {
			dispatch->dispatch({2, 5, 100, 10}, [&count1](Flattener<>& flattener, size_t flatIndex){
				std::this_thread::sleep_for (std::chrono::milliseconds(10));
				count1++;
			});
		} catch(const std::logic_error&) {
			// terminate() ran before thread1's dispatch() call reached the front of the
			// race; a dispatch() call after termination throws instead of proceeding.
		}
	});

	// Terminate early -- safe regardless of whether thread1 has started dispatch() yet,
	// because terminate() waits for a dispatch() call already in flight to finish, and a
	// dispatch() call that starts afterward throws instead of racing with termination.
	// Still must join thread1 before deleting though: a thread that hasn't attempted
	// dispatch() at all yet isn't something terminate() can wait for -- see Dispatcher.hpp.
	dispatch->terminate();
	thread1.join();
	delete dispatch;

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

TEST_CASE( "Repeated terminate()-races-with-dispatch() stays race-free" ) {
	// Regression coverage for a fixed bug: terminate() used to be able to return (letting
	// ~Dispatcher() proceed to destroy the object) while another thread was still inside
	// dispatch() on the same instance, and separately read currentJob without holding
	// jobMutex. terminate() now waits for a dispatch() call already in flight to finish,
	// and rejects (throws on) one that starts afterward instead of racing with it. This
	// loop hammers many interleavings of "terminate() vs. a concurrent dispatch() call
	// that may or may not have started yet" with no synchronizing sleep -- run under
	// -fsanitize=thread for the strongest check.
	for(int i = 0; i < 200; ++i) {
		Dispatcher* dispatch = new Dispatcher(8);
		std::atomic<int> count1(0);
		std::thread thread1([dispatch, &count1]() {
			try {
				dispatch->dispatch({2, 5, 20, 10}, [&count1](Flattener<>& flattener, size_t flatIndex){
					count1++;
				});
			} catch(const std::logic_error&) {
				// Expected when terminate() wins the race to start first.
			}
		});

		dispatch->terminate();
		thread1.join();
		delete dispatch;
	}
}

TEST_CASE( "Concurrent dispatch() calls on the same instance throw instead of corrupting state" ) {
	// The only thing preventing two concurrent dispatch() calls on the same instance from
	// corrupting currentJob is the throw_if() guard in dispatch() (Dispatcher.hpp:184-185).
	// Unlike ensure(), throw_if() is active regardless of NDEBUG, so this holds in both
	// Debug and Release builds. Use a promise set from inside the worker (rather than a
	// fixed sleep) to know for certain threadA has set currentJob before attempting the
	// second, concurrent call.
	Dispatcher dispatch(8);

	std::promise<void> aStarted;
	auto aStartedFuture = aStarted.get_future();
	std::atomic<int> countA(0);
	std::thread threadA([&]() {
		dispatch.dispatch({2, 5, 200, 10}, [&](Flattener<>& flattener, size_t flatIndex){
			if(flatIndex == 0)
				aStarted.set_value();
			std::this_thread::sleep_for(std::chrono::microseconds(50));
			countA++;
		});
	});

	aStartedFuture.wait();

	CHECK_THROWS_AS(dispatch.dispatch({4}, [](Flattener<>& flattener, size_t flatIndex){}), std::logic_error);

	threadA.join();
}

TEST_CASE( "dispatch() after terminate() throws instead of silently doing no work" ) {
	Dispatcher dispatch(4);
	dispatch.terminate();

	std::atomic<int> count(0);
	CHECK_THROWS_AS(dispatch.dispatch(100, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	}), std::logic_error);

	CHECK(0 == count);
}

TEST_CASE( "Repeated explicit terminate() calls are idempotent" ) {
	Dispatcher dispatch(4);
	std::atomic<int> count(0);
	dispatch.dispatch(10, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});

	dispatch.terminate();
	dispatch.terminate();
	dispatch.terminate();

	CHECK(10 == count);
}

TEST_CASE( "getThreadCount() still reports the original count after terminate()" ) {
	Dispatcher dispatch(6);
	CHECK(6 == dispatch.getThreadCount());

	dispatch.terminate();

	CHECK(6 == dispatch.getThreadCount());
}

TEST_CASE( "Reentrant dispatch() from within a worker callback throws instead of hanging" ) {
	// A worker callback calling dispatch() again on the *same* instance looks identical to
	// a concurrent call from another thread as far as the currentJob guard is concerned --
	// it throws cleanly rather than deadlocking or corrupting state.
	Dispatcher dispatch(4);
	bool innerThrew = false;

	dispatch.dispatch(4, [&](Flattener<>& flattener, size_t flatIndex){
		if(flatIndex == 0) {
			try {
				dispatch.dispatch(2, [](Flattener<>& f, size_t i){});
			} catch(const std::logic_error&) {
				innerThrew = true;
			}
		}
	});

	CHECK(innerThrew);
}

TEST_CASE( "A worker callback that throws propagates to the dispatch() caller instead of aborting" ) {
	std::atomic<int> count(0);
	Dispatcher dispatch(4);

	CHECK_THROWS_AS(dispatch.dispatch(20, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
		if(flatIndex == 5)
			throw std::runtime_error("worker callback failure");
	}), std::runtime_error);

	// The other 19 work units still ran to completion despite the one failure.
	CHECK(20 == count);
}

TEST_CASE( "terminate() called from within one of its own worker callbacks throws instead of hanging" ) {
	// terminate() would otherwise end up joining the very thread running this callback.
	// That's rejected immediately (before touching any state) rather than either crashing
	// on a self-join std::system_error, or -- worse, and what actually happened before this
	// guard was added -- silently abandoning the in-flight job's remaining work units and
	// hanging the dispatch() call below forever, since neither job->complete() nor
	// allWorkersJoined could ever become true afterward.
	Dispatcher* dispatch = new Dispatcher(4);
	std::atomic<int> count(0);

	CHECK_THROWS_AS(dispatch->dispatch(4, [&](Flattener<>& flattener, size_t flatIndex){
		count++;
		if(flatIndex == 0)
			dispatch->terminate();
	}), std::logic_error);

	// The job still ran to completion -- the rejected terminate() call had no side effects.
	CHECK(4 == count);

	delete dispatch;
}
