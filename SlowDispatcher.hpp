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
#include <functional>
#include <stdexcept>
#include "Flattener.hpp"

namespace thr {
	/**
	 * @brief A single-threaded, same-interface stand-in for Dispatcher: dispatch() just loops
	 * over every flat index on the calling thread itself, rather than spreading the work across
	 * a pool. Useful for stepping through a worker function under a debugger, or for a parameter
	 * space too small to be worth Dispatcher's thread overhead.
	 *
	 * Because dispatch() never leaves the calling thread, several of Dispatcher's usage
	 * restrictions don't apply here: concurrent dispatch() calls from different threads on the
	 * same instance are fine (each call only ever touches its own local parameters, not shared
	 * state), calling terminate() from within dispatch()'s own callback is an ordinary
	 * same-thread early exit rather than a self-join hazard, and an exception thrown by the
	 * callback propagates normally to dispatch()'s caller rather than needing to be caught and
	 * rethrown across a thread boundary.
	 */
	class SlowDispatcher {
	public:
		/**
		 * @brief Constructs a SlowDispatcher. There's no thread pool to size, but this exists so
		 * the same construction syntax as Dispatcher() still works.
		 */
		SlowDispatcher() {}

		/**
		 * @brief Constructs a SlowDispatcher. The argument is ignored -- it exists only so a
		 * Dispatcher(threadCount) call site can be swapped over to SlowDispatcher without
		 * editing it. getThreadCount() always reports 1 regardless of what's passed here.
		 */
		SlowDispatcher(unsigned int) {}

		/**
		 * @brief Destroys the SlowDispatcher, first calling terminate(). Harmless in every case,
		 * since dispatch() never leaves any state running past its own return.
		 */
		virtual ~SlowDispatcher() {
			terminate();
		}

		/**
		 * @brief Requests that any dispatch() call currently in progress (on any thread) stop
		 * after finishing its current index instead of continuing to the end of the shape.
		 *
		 * Safe to call multiple times, from any thread, including from within dispatch()'s own
		 * callback -- that's an ordinary same-thread early exit here, not the self-join hazard
		 * it would be for Dispatcher.
		 */
		void terminate() {
			terminateAll = true;
		}

		/**
		 * @brief Constructs a temporary SlowDispatcher and uses it for one dispatch() call over
		 * a 1-dimensional space of size @p count.
		 *
		 * @param count The length of the (1-dimensional) parameter space.
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(const unsigned int count, const std::function<void(Flattener<>&, size_t)>& worker) {
			SlowDispatcher dispatcher;
			dispatcher.dispatch(count, worker);
		}

		/**
		 * @brief Constructs a temporary SlowDispatcher and uses it for one dispatch() call over
		 * the space described by @p shape.
		 *
		 * @param shape The shape of the parameter space, outermost dimension first.
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(std::vector<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			SlowDispatcher dispatcher;
			dispatcher.dispatch(shape, worker);
		}

		/**
		 * @brief Constructs a temporary SlowDispatcher and uses it for one dispatch() call over
		 * @p dimensions.
		 *
		 * @param dimensions The shape of the parameter space to cover.
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& worker) {
			SlowDispatcher dispatcher;
			dispatcher.dispatch(dimensions, worker);
		}

		/**
		 * @brief Equivalent to dispatch(Flattener<>({count}), worker) -- covers a 1-dimensional
		 * space of size @p count.
		 *
		 * @param count The length of the (1-dimensional) parameter space.
		 * @param worker See the Flattener<> overload.
		 * @throws Same as the Flattener<> overload.
		 */
		void dispatch(const unsigned int count, const std::function<void(Flattener<>&, size_t)>& worker) {
			dispatch({count}, worker);
		}

		/**
		 * @brief Equivalent to dispatch(Flattener<>(shape), worker).
		 *
		 * @param shape The shape of the parameter space, outermost dimension first.
		 * @param worker See the Flattener<> overload.
		 * @throws Same as the Flattener<> overload.
		 */
		void dispatch(std::initializer_list<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			dispatch(Flattener<>(shape), worker);
		}

		/**
		 * @brief Equivalent to dispatch(Flattener<>(shape), worker).
		 *
		 * @param shape The shape of the parameter space, outermost dimension first.
		 * @param worker See the Flattener<> overload.
		 * @throws Same as the Flattener<> overload.
		 */
		void dispatch(std::vector<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			dispatch(Flattener<>(shape), worker);
		}

		/**
		 * @brief Runs @p worker once for every flat index in @p dimensions, synchronously on the
		 * calling thread, until every index has been visited or terminate() is called.
		 *
		 * @param dimensions The shape of the parameter space to cover.
		 * @param worker Called as worker(dimensions, flatIndex) once per flat index, on the
		 * calling thread. Safe to call terminate() on this SlowDispatcher from within worker
		 * itself, to stop the loop early.
		 *
		 * @throws std::logic_error if this SlowDispatcher has already been terminated (see
		 * terminate()), matching Dispatcher's contract for the same situation.
		 * @throws Whatever worker throws, from whichever index throws it -- propagates
		 * immediately, exactly like an ordinary function call, since there's no worker thread
		 * boundary to cross.
		 */
		void dispatch(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& worker) {
			throw_if<std::logic_error>(terminateAll, "dispatch() called on a terminated SlowDispatcher");

			for(size_t flatIndex = 0; flatIndex < dimensions.size() && !terminateAll; ++flatIndex) {
				worker(dimensions, flatIndex);
			}
		}

		/**
		 * @brief Always returns 1 -- SlowDispatcher never spawns a thread.
		 */
		unsigned int getThreadCount() { return threadCount; }

		/**
		 * @brief Present for API parity with Dispatcher/BatchDispatcher. Has no effect on
		 * SlowDispatcher, which never spawns a thread regardless of the constructor argument.
		 */
		constexpr static unsigned int defaultWorkerThreadCount = 4;

	private:
		const unsigned int threadCount = 1;
		// Atomic because terminate() can legitimately be called from a different thread
		// than the one currently inside dispatch()'s loop (see "Overlapping jobs" and
		// "Early termination" tests) -- a plain bool read/written across threads without
		// synchronization would be a data race even though SlowDispatcher itself never
		// spawns any threads.
		std::atomic<bool> terminateAll = {false};
	};
};
