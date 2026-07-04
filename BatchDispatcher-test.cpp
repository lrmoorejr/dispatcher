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
#include <stdexcept>
#include <limits>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "BatchDispatcher.hpp"

using namespace thr;

TEST_CASE( "Create / destroy dispatch" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher();
	auto threadCount = dispatch->getThreadCount();
	printf("Default thread count %d\n", threadCount);
	delete dispatch;
	CHECK(0 < threadCount);
}

TEST_CASE( "Set thread count" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(7);
	auto threadCount = dispatch->getThreadCount();
	delete dispatch;
	CHECK(7 == threadCount);
}

TEST_CASE( "Unknown thread count" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher<>(0);
	auto threadCount = dispatch->getThreadCount();
	delete dispatch;
	CHECK(dispatch->defaultWorkerThreadCount == threadCount);
}

TEST_CASE( "Simple dispatch" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	dispatch->dispatch(1, 1, [](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){printf("Simple dispatch called\n");});
	delete dispatch;
}

TEST_CASE( "Simple dispatch, 3 count" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(3, 1, [&count](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		count++;
	});
	delete dispatch;

	CHECK(3 == count);
}

TEST_CASE( "Simple dispatch, 0 count" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	CHECK_THROWS(dispatch->dispatch(0, 1, [&count](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){ count++;}));
	delete dispatch;

	CHECK(0 == count);
}

TEST_CASE( "Simple dispatch, 0 on one dimension" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	CHECK_THROWS(dispatch->dispatch({3,4,0,3,4}, 1, [&count](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){ count++;}));
	delete dispatch;

	CHECK(0 == count);
}

TEST_CASE( "Simple dispatch, 4 threads, 4 count" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(4);
	CHECK(4 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(4, 100, [&count](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		count += batch;
	});
	delete dispatch;

	CHECK(4 == count);
}

TEST_CASE( "Simple dispatch, 2 threads, 100 count" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(2);
	CHECK(2 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(100, 7, [&count](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		count += batch;
	});
	delete dispatch;

	CHECK(100 == count);
}

TEST_CASE( "Simple dispatch, 100 threads, 2 count" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(100);
	CHECK(100 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(2, 13, [&count](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		count += batch;
	});
	delete dispatch;

	CHECK(2 == count);
}

TEST_CASE( "Weird shape (100000 total), 8 threads" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(8);
	CHECK(8 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch({2, 5, 10, 100, 10}, 41, [&count](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		count += batch;
	});
	delete dispatch;

	CHECK(100000 == count);
}

TEST_CASE( "Reused dispatch" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(100);

	std::atomic<int> count1(0);
	dispatch->dispatch({2, 5, 100, 10}, 5, [&count1](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		std::this_thread::sleep_for (std::chrono::milliseconds(10));
		count1 += batch;
	});

	std::atomic<int> count2(0);
	dispatch->dispatch({2, 5, 100, 10}, 2, [&count2](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		std::this_thread::sleep_for (std::chrono::milliseconds(10));
		count2 += batch;
	});

	delete dispatch;

	CHECK(10000 == count1);
	CHECK(10000 == count2);
}

TEST_CASE( "Shape completedness test" ) {
	BatchDispatcher<>* dispatch = new BatchDispatcher(8);
	CHECK(8 == dispatch->getThreadCount());
	bool ran[2][5][10][100][10] = {false};
	dispatch->dispatch({2, 5, 10, 100, 10}, 11, [&ran](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		for(unsigned int index = 0; index < batch; ++index) {
			int a = flattener.index(0, flatIndex + index);
			REQUIRE(a >= 0);
			REQUIRE(a < 2);
			int b = flattener.index(1, flatIndex + index);
			REQUIRE(b>= 0);
			REQUIRE(b< 5);
			int c = flattener.index(2, flatIndex + index);
			REQUIRE(c >= 0);
			REQUIRE(c < 10);
			int d = flattener.index(3, flatIndex + index);
			REQUIRE(d >= 0);
			REQUIRE(d < 100);
			int e = flattener.index(4, flatIndex + index);
			REQUIRE(e >= 0);
			REQUIRE(e < 10);
			ran[a][b][c][d][e] = true;
		}
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

TEST_CASE( "Default dispatch, 300 count" ) {
	std::atomic<int> count(0);
	BatchDispatcher<>::defaultDispatch(300, 7, [&count](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		count += batch;
	});

	CHECK(300 == count);
}

TEST_CASE( "Unbashed default dispatch, 300 count" ) {
	std::atomic<int> count(0);
	BatchDispatcher<>::defaultDispatch(300, [&count](const Flattener<>& flattener, size_t flatIndex, unsigned int batch){
		CHECK(batch == 1);
		count += 1;
	});

	CHECK(300 == count);
}

TEST_CASE( "Concurrent dispatch() calls on the same instance throw instead of corrupting state" ) {
	// flattener/maxBatch/worker/allocated are instance members, so a second dispatch()
	// call on the same instance while one is already in flight would otherwise race on
	// them. Use a promise set from inside the worker (rather than a fixed sleep) to know
	// for certain threadA's call has actually started before attempting the second one.
	BatchDispatcher<> dispatch(8);

	std::promise<void> aStarted;
	auto aStartedFuture = aStarted.get_future();
	std::atomic<int> countA(0);
	std::thread threadA([&]() {
		dispatch.dispatch(2000, 7, [&](const Flattener<>& flattener, size_t start, unsigned int batch){
			if(start == 0)
				aStarted.set_value();
			std::this_thread::sleep_for(std::chrono::microseconds(50));
			countA += batch;
		});
	});

	aStartedFuture.wait();

	CHECK_THROWS_AS(dispatch.dispatch(4, 1, [](const Flattener<>& flattener, size_t start, unsigned int batch){}), std::logic_error);

	threadA.join();
	CHECK(2000 == countA);
}

TEST_CASE( "A worker callback that throws propagates to the dispatch() caller instead of aborting" ) {
	std::atomic<int> count(0);
	BatchDispatcher<> dispatch(4);

	CHECK_THROWS_AS(dispatch.dispatch(20, 1, [&count](const Flattener<>& flattener, size_t start, unsigned int batch){
		count += batch;
		if(start == 5)
			throw std::runtime_error("worker callback failure");
	}), std::runtime_error);

	// The other 19 work units still ran to completion despite the one failure.
	CHECK(20 == count);
}

TEST_CASE( "Reentrant dispatch() from within a worker callback throws instead of corrupting state" ) {
	// A worker callback calling dispatch() again on the same instance looks identical to a
	// concurrent call from another thread as far as the inProgress guard is concerned -- it
	// throws cleanly rather than corrupting flattener/maxBatch/worker/allocated.
	BatchDispatcher<> dispatch(4);
	bool innerThrew = false;

	dispatch.dispatch(4, 1, [&](const Flattener<>& flattener, size_t start, unsigned int batch){
		if(start == 0) {
			try {
				dispatch.dispatch(2, 1, [](const Flattener<>& f, size_t s, unsigned int b){});
			} catch(const std::logic_error&) {
				innerThrew = true;
			}
		}
	});

	CHECK(innerThrew);
}

TEST_CASE( "maxBatch of 0 throws instead of hanging forever" ) {
	// batchSize starts as maxBatch and the tail-shrinking branch (todo < maxBatch) can never
	// trigger when maxBatch is 0, so allocated would never advance and every worker thread
	// would spin forever -- reject it upfront instead.
	BatchDispatcher<> dispatch(2);

	CHECK_THROWS_AS(dispatch.dispatch(10, 0, [](const Flattener<>& flattener, size_t start, unsigned int batch){}),
		std::invalid_argument);
}

TEST_CASE( "A count wider than unsigned int throws instead of silently truncating" ) {
	// The dispatch(T count, ...) overload builds a single-dimension Flattener<T>, whose
	// dimension size is always an unsigned int regardless of T -- static_cast<unsigned
	// int>(count) would otherwise silently wrap for a count that doesn't fit, iterating a
	// space far smaller than requested with no error. This applies even to the default
	// BatchDispatcher<> (T = size_t) on any 64-bit platform, not just an unusual custom T.
	BatchDispatcher<> dispatch(2);
	size_t tooLarge = static_cast<size_t>(std::numeric_limits<unsigned int>::max()) + 1;

	CHECK_THROWS_AS(dispatch.dispatch(tooLarge, 10, [](const Flattener<>& flattener, size_t start, unsigned int batch){}),
		std::overflow_error);
}

TEST_CASE( "Small dispatch (fewer units than threads) still processes every unit exactly once" ) {
	// Regression coverage for clamping the spawned thread count to the available work --
	// confirms the optimization didn't change correctness for a space much smaller than
	// threadCount.
	BatchDispatcher<> dispatch(16);
	std::atomic<int> count(0);

	dispatch.dispatch(3, 5, [&count](const Flattener<>& flattener, size_t start, unsigned int batch){
		count += batch;
	});

	CHECK(3 == count);
}

TEST_CASE( "Uneven tail with many threads still processes every unit exactly once" ) {
	// Regression coverage for the tail batch-size heuristic now dividing by the number of
	// still-active workers rather than the fixed configured thread count.
	BatchDispatcher<> dispatch(8);
	std::atomic<size_t> count(0);

	dispatch.dispatch(1'000'003, 4096, [&count](const Flattener<>& flattener, size_t start, unsigned int batch){
		count += batch;
	});

	CHECK(1'000'003 == count);
}

TEST_CASE( "Repeated concurrent dispatch() calls, some throwing, never lose an exception" ) {
	// Regression coverage for a fixed bug: inProgress used to be reset to false (unblocking
	// a new dispatch() call) before caughtException was read for the rethrow, so a freshly
	// started call could wipe caughtException (its own setup resets it to nullptr) before
	// the finishing call read it -- silently swallowing a real exception. caughtException is
	// now captured into a local before inProgress is released. This loop won't reliably catch
	// a regression without an artificially widened window (confirmed empirically during the
	// fix: 429 lost exceptions in 3 seconds with the window widened) -- it exists to give the
	// race a chance under -fsanitize=thread, not as a reliable assertion on its own.
	std::atomic<bool> stop = {false};
	std::atomic<int> missedExceptions = {0};
	BatchDispatcher<> dispatch(4);

	auto hammer = [&](bool shouldThrow) {
		while(!stop.load()) {
			try {
				dispatch.dispatch(40, 5, [&](const Flattener<>& flattener, size_t start, unsigned int batch){
					if(shouldThrow && start == 0)
						throw std::runtime_error("expected");
				});
				if(shouldThrow)
					missedExceptions++;
			} catch(const std::runtime_error&) {
				// expected
			} catch(const std::logic_error&) {
				// expected: a concurrent call was rejected instead of running at all
			}
		}
	};

	std::thread thread1(hammer, true);
	std::thread thread2(hammer, false);

	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	stop = true;
	thread1.join();
	thread2.join();

	CHECK(0 == missedExceptions);
}