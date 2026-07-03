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