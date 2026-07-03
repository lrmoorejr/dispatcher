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

// This translation unit is built with NDEBUG defined (see CMakeLists.txt), specifically to
// exercise Dispatcher's behavior when ensure() compiles to a no-op. Kept separate from
// Dispatcher-test.cpp so the main suite keeps running with ensure() active (its normal,
// documented behavior) rather than making the whole suite's build type ambiguous.

#include <future>
#include <catch2/catch_test_macros.hpp>
#include "Dispatcher.hpp"

using namespace thr;

TEST_CASE( "NDEBUG: concurrent dispatch() calls silently corrupt currentJob instead of erroring" ) {
	// The only thing preventing two concurrent dispatch() calls on the same Dispatcher
	// instance from corrupting `currentJob` is `ensure(currentJob == nullptr, ...)`
	// (Dispatcher.hpp:171). Under NDEBUG that macro is `((void)0)` and the condition is
	// never evaluated, so `currentJob = job;` runs unconditionally even when a job is
	// already in flight. Confirmed to reproduce reliably (3/3 local runs) with this timing.
	//
	// threadB's dispatch() silently overwrites currentJob out from under threadA's
	// in-flight job. No worker ever looks at threadA's orphaned job again (workers only
	// consult currentJob, now threadB's job), so threadA's dispatch() call blocks forever
	// and its Job (plus captured worker closure) leaks. Use a bounded wait rather than an
	// unbounded join, so this failure is a normal test failure instead of a hung suite.

	// Heap-allocate and deliberately leak on the buggy path: threadA can be permanently
	// stuck inside dispatch() on this instance, and destroying the Dispatcher out from
	// under it would trigger the separate (also real) terminate()/dispatch() destructor
	// race instead of cleanly isolating this one.
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
		FAIL("threadA's dispatch() call never completed -- concurrent dispatch() silently "
		     "overwrote currentJob and orphaned threadA's job (leak + permanent hang)");
	} else {
		threadA.join();
		delete dispatch;
	}
}
