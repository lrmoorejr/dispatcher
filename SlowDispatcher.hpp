#pragma once

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

#include <atomic>
#include "Flattener.hpp"

namespace thr {
	/**
	 * @brief SlowDispatcher presents an identical interface to Dispatcher, but it is single threaded, and essentially
	 * undocumented aside from this note.  See Dispatcher for details on its operation.  Also, on rare occasions it
	 * is nice to have dispatch logic for small amounts of data that don't warrant the thread overhead of Dispatcher.
	 * The purpose of SlowDispatcher is to aid in debugging or stepwise development.
	 * 
	 */
	class SlowDispatcher {
	public:
		SlowDispatcher() {}
		SlowDispatcher(unsigned int) {}
		virtual ~SlowDispatcher() {
			terminate();
		}

		void terminate() {
			terminateAll = true;
		}

		static void defaultDispatch(const unsigned int count, const std::function<void(Flattener<>&, size_t)>& worker) {
			SlowDispatcher dispatcher;
			dispatcher.dispatch(count, worker);
		}

		static void defaultDispatch(std::vector<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			SlowDispatcher dispatcher;
			dispatcher.dispatch(shape, worker);
		}

		static void defaultDispatch(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& worker) {
			SlowDispatcher dispatcher;
			dispatcher.dispatch(dimensions, worker);
		}

		void dispatch(const unsigned int count, const std::function<void(Flattener<>&, size_t)>& worker) {
			dispatch({count}, worker);
		}

		void dispatch(std::initializer_list<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			dispatch(Flattener<>(shape), worker);
		}

		void dispatch(std::vector<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			dispatch(Flattener<>(shape), worker);
		}

		void dispatch(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& worker) {
			// Construct a new job
			Job job(dimensions, worker);

			for(size_t flatIndex = 0; flatIndex < job.dimensions.size() && !terminateAll; ++flatIndex) {
				job.workFunction(job.dimensions, flatIndex);
			}
		}

		unsigned int getThreadCount() { return threadCount; }

	private:
		class Job {
		private:
			Job(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& workFunction) : dimensions(dimensions), workFunction(workFunction) {}
			inline bool depleted() const { return allocated >= dimensions.size(); }
			inline bool complete() const  { return completed >= dimensions.size(); }

			Flattener<> dimensions;
			const std::function<void(Flattener<>&, size_t)> workFunction;

			size_t allocated = 0;	// How many work units have been allocated to threads, protected by Dispatch::jobQueueMutex
			size_t completed = 0;	// How many work units were completed, protected by Job::mutex

			friend class SlowDispatcher;
		};

		const unsigned int threadCount = 1;
		// Atomic because terminate() can legitimately be called from a different thread
		// than the one currently inside dispatch()'s loop (see "Overlapping jobs" and
		// "Early termination" tests) -- a plain bool read/written across threads without
		// synchronization would be a data race even though SlowDispatcher itself never
		// spawns any threads.
		std::atomic<bool> terminateAll = {false};
	};
};
