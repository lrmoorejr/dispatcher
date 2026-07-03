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
