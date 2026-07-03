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

#include <thread>
#include <stdexcept>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "SlowDispatcher.hpp"

using namespace thr;

TEST_CASE("SlowDispatcher Create / destroy dispatch" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();
	auto threadCount = dispatch->getThreadCount();
	printf("Default thread count %d\n", threadCount);
	delete dispatch;
	CHECK(0 < threadCount);
}

TEST_CASE("SlowDispatcher Set thread count" ) {
	SlowDispatcher* dispatch = new SlowDispatcher(7);
	auto threadCount = dispatch->getThreadCount();
	delete dispatch;
	CHECK(1 == threadCount);
}

TEST_CASE("SlowDispatcher Unknown thread count" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();
	auto threadCount = dispatch->getThreadCount();
	delete dispatch;
	CHECK(1 == threadCount);
}

TEST_CASE("SlowDispatcher Simple dispatch" ) {
	SlowDispatcher* dispatch = new SlowDispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	dispatch->dispatch(1, [](Flattener<>& flattener, size_t flatIndex){printf("Simple dispatch called\n");});
	delete dispatch;
}

TEST_CASE("SlowDispatcher Simple dispatch, 3 count" ) {
	SlowDispatcher* dispatch = new SlowDispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(3, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(3 == count);
}

TEST_CASE("SlowDispatcher Simple dispatch, 0 count" ) {
	SlowDispatcher* dispatch = new SlowDispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	CHECK_THROWS(dispatch->dispatch(0, [&count](Flattener<>& flattener, size_t flatIndex){ count++;}));
	delete dispatch;

	CHECK(0 == count);
}

TEST_CASE("SlowDispatcher Simple dispatch, 0 on one dimension" ) {
	SlowDispatcher* dispatch = new SlowDispatcher(1);
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	CHECK_THROWS(dispatch->dispatch({3,4,0,3,4}, [&count](Flattener<>& flattener, size_t flatIndex){ count++;}));
	delete dispatch;

	CHECK(0 == count);
}

TEST_CASE("SlowDispatcher Simple dispatch, 4 threads, 4 count" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(4, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(4 == count);
}

TEST_CASE("SlowDispatcher Simple dispatch, 2 threads, 100 count" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(100, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(100 == count);
}

TEST_CASE("SlowDispatcher Simple dispatch, 100 threads, 2 count" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch(2, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(2 == count);
}

TEST_CASE("SlowDispatcher Weird shape (100000 total), 8 threads" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();
	CHECK(1 == dispatch->getThreadCount());
	std::atomic<int> count(0);
	dispatch->dispatch({2, 5, 10, 100, 10}, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});
	delete dispatch;

	CHECK(100000 == count);
}

TEST_CASE("SlowDispatcher Overlapping jobs" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();

	std::atomic<int> count1(0);
	std::thread thread1([dispatch, &count1]() {
		dispatch->dispatch({2, 5, 10, 10}, [&count1](Flattener<>& flattener, size_t flatIndex){
			std::this_thread::sleep_for (std::chrono::milliseconds(10));
			count1++;
		});
	});

	std::atomic<int> count2(0);
	std::thread thread2([dispatch, &count2]() {
		dispatch->dispatch({2, 5, 10, 10}, [&count2](Flattener<>& flattener, size_t flatIndex){
			std::this_thread::sleep_for (std::chrono::milliseconds(10));
			count2++;
		});
	});

	thread1.join();
	thread2.join();

	delete dispatch;

	CHECK(1000 == count1);
	CHECK(1000 == count2);
}

TEST_CASE("SlowDispatcher Shape completedness test" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();
	CHECK(1 == dispatch->getThreadCount());
	bool ran[2][2][2][2][2] = {false};
	dispatch->dispatch({2, 2, 2, 2, 2}, [&ran](Flattener<>& flattener, size_t flatIndex){
		int a = flattener.index(0, flatIndex);
		REQUIRE(a >= 0);
		REQUIRE(a < 2);
		int b= flattener.index(1, flatIndex);
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

	for(int e = 0; e < 2; ++e) {
		for(int d = 0; d < 2; ++d) {
			for(int c = 0; c < 2; ++c) {
				for(int b = 0; b < 2; ++b) {
					for(int a = 0; a < 2; ++a) {
						CHECK(ran[a][b][c][d][e] == true);
					}
				}
			}
		}
	}
}

TEST_CASE("SlowDispatcher Fast early termination" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();

	std::atomic<int> count1(0);
	std::thread thread1([dispatch, &count1]() {
		dispatch->dispatch({2, 5, 100, 10}, [&count1](Flattener<>& flattener, size_t flatIndex){
			std::this_thread::sleep_for (std::chrono::milliseconds(10));
			count1++;
		});
	});

	// Terminate early
	dispatch->terminate();
	if(thread1.joinable())
		thread1.join();
	delete dispatch;

	CHECK(10000 > count1);
}

TEST_CASE("SlowDispatcher Early termination half way through" ) {
	SlowDispatcher* dispatch = new SlowDispatcher();

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

TEST_CASE("SlowDispatcher Repeated explicit terminate() calls are idempotent") {
	SlowDispatcher dispatch;
	std::atomic<int> count(0);
	dispatch.dispatch(10, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
	});

	dispatch.terminate();
	dispatch.terminate();
	dispatch.terminate();

	CHECK(10 == count);
}

TEST_CASE("SlowDispatcher terminate() called from within its own dispatch() callback stops the loop safely") {
	// Unlike Dispatcher, SlowDispatcher never spawns a thread, so there's no self-join
	// hazard here -- the callback runs on the same thread as the for-loop it's calling
	// terminate() on, so this is just an ordinary early-exit, no guard needed.
	SlowDispatcher dispatch;
	std::atomic<int> count(0);

	dispatch.dispatch(100, [&](Flattener<>& flattener, size_t flatIndex){
		count++;
		if(flatIndex == 4)
			dispatch.terminate();
	});

	CHECK(5 == count); // indices 0..4 ran before terminateAll stopped the loop
}

TEST_CASE("SlowDispatcher worker callback exceptions propagate normally, no thread boundary involved") {
	// dispatch() is a plain synchronous loop on the caller's own thread, so an exception
	// from the callback needs no special handling -- it just propagates like it would from
	// any ordinary function call, unlike Dispatcher/BatchDispatcher where it would otherwise
	// escape a separate worker thread and abort the process.
	SlowDispatcher dispatch;
	std::atomic<int> count(0);

	CHECK_THROWS_AS(dispatch.dispatch(20, [&count](Flattener<>& flattener, size_t flatIndex){
		count++;
		if(flatIndex == 5)
			throw std::runtime_error("callback failure");
	}), std::runtime_error);

	// Single-threaded and synchronous: the loop stops dead at the throw, unlike the
	// parallel dispatchers where other already-running units still finish regardless.
	CHECK(6 == count);
}
