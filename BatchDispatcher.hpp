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

#include <functional>
#include <thread>
#include <vector>
#include <atomic>
#include <exception>
#include <stdexcept>
#include "Flattener.hpp"

namespace thr {
	/**
	 * @brief Like Dispatcher, but hands each thread a contiguous batch of flat indices per
	 * callback instead of one index at a time, so the per-callback overhead is amortized across
	 * the whole batch. Prefer this over Dispatcher when each individual unit of work is cheap
	 * enough that Dispatcher's per-index dispatch overhead would dominate.
	 *
	 * Only one dispatch() call may be in flight on a given instance at a time (see dispatch()).
	 * Unlike Dispatcher, a BatchDispatcher doesn't keep worker threads around between calls --
	 * each dispatch() call spawns a fresh batch of threads and joins all of them before
	 * returning, so there's no separate terminate()/destructor-lifetime contract to worry about;
	 * destroying a BatchDispatcher is always safe once no dispatch() call on it is in flight.
	 *
	 * @tparam T The type used to index the space. Must be able to represent the total number of
	 * points in the space.
	 */
	template<typename T=size_t>
	class BatchDispatcher {
	public:
		/**
		 * @brief Constructs a BatchDispatcher. Unlike Dispatcher, no threads are started yet --
		 * threadCount only determines how many are spawned per dispatch() call.
		 *
		 * @param threadCount Number of worker threads to spawn per dispatch() call. 0 uses
		 * defaultWorkerThreadCount. Defaults to std::thread::hardware_concurrency().
		 */
		BatchDispatcher(unsigned int threadCount = std::thread::hardware_concurrency())
			: threadCount(threadCount == 0 ? defaultWorkerThreadCount : threadCount) {}

		/**
		 * @brief Constructs a temporary BatchDispatcher and uses it for one dispatch() call over
		 * a 1-dimensional space of size @p count, with a batch size of 1. Only useful for a
		 * single one-off dispatch, since the thread pool doesn't outlive this call.
		 *
		 * @param count The length of the (1-dimensional) parameter space.
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(T count, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			BatchDispatcher dispatcher;
			dispatcher.dispatch(count, 1, worker);
		}

		/**
		 * @brief Constructs a temporary BatchDispatcher and uses it for one dispatch() call over
		 * a 1-dimensional space of size @p count. Only useful for a single one-off dispatch,
		 * since the thread pool doesn't outlive this call.
		 *
		 * @param count The length of the (1-dimensional) parameter space.
		 * @param maxBatch See dispatch().
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(T count, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			BatchDispatcher dispatcher;
			dispatcher.dispatch(count, maxBatch, worker);
		}

		/**
		 * @brief Constructs a temporary BatchDispatcher and uses it for one dispatch() call over
		 * the space described by @p shape. Only useful for a single one-off dispatch, since the
		 * thread pool doesn't outlive this call.
		 *
		 * @param shape The shape of the parameter space, outermost dimension first.
		 * @param maxBatch See dispatch().
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(const std::vector<unsigned int>& shape, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			BatchDispatcher dispatcher;
			dispatcher.dispatch(shape, maxBatch, worker);
		}

		/**
		 * @brief Constructs a temporary BatchDispatcher and uses it for one dispatch() call over
		 * @p flattener. Only useful for a single one-off dispatch, since the thread pool doesn't
		 * outlive this call.
		 *
		 * @param flattener The shape of the parameter space to cover.
		 * @param maxBatch See dispatch().
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(const Flattener<T>& flattener, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			BatchDispatcher dispatcher;
			dispatcher.dispatch(flattener, maxBatch, worker);
		}

		/**
		 * @brief Equivalent to dispatch(Flattener<T>({count}), maxBatch, worker) -- covers a
		 * 1-dimensional space of size @p count.
		 *
		 * @param count The length of the (1-dimensional) parameter space.
		 * @param maxBatch See the Flattener<T> overload.
		 * @param worker See the Flattener<T> overload.
		 * @throws Same as the Flattener<T> overload.
		 */
		void dispatch(T count, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			dispatch(Flattener<T>({static_cast<unsigned int>(count)}), maxBatch, worker);
		}

		/**
		 * @brief Equivalent to dispatch(Flattener<T>(shape), maxBatch, worker).
		 *
		 * @param shape The shape of the parameter space, outermost dimension first.
		 * @param maxBatch See the Flattener<T> overload.
		 * @param worker See the Flattener<T> overload.
		 * @throws Same as the Flattener<T> overload.
		 */
		void dispatch(std::initializer_list<unsigned int> shape, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			dispatch(Flattener<T>(shape), maxBatch, worker);
		}

		/**
		 * @brief Equivalent to dispatch(Flattener<T>(shape), maxBatch, worker).
		 *
		 * @param shape The shape of the parameter space, outermost dimension first.
		 * @param maxBatch See the Flattener<T> overload.
		 * @param worker See the Flattener<T> overload.
		 * @throws Same as the Flattener<T> overload.
		 */
		void dispatch(std::vector<unsigned int>& shape, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			dispatch(Flattener<T>(shape), maxBatch, worker);
		}

		/**
		 * @brief Runs @p worker once per contiguous batch of up to @p maxBatch flat indices
		 * covering @p flattener, spread across a freshly spawned pool of threads, blocking the
		 * calling thread until every index has been visited.
		 *
		 * @param flattener The shape of the parameter space to cover.
		 * @param maxBatch Maximum number of consecutive flat indices handed to @p worker per
		 * call. Actual batches may be smaller (e.g. the last one, or when there's less work left
		 * than threads to spread it across).
		 * @param worker Called as worker(flattener, start, count) once per batch, where the batch
		 * covers flat indices [start, start + count). May run on any worker thread, and
		 * different batches may run concurrently on different threads -- worker must be safe to
		 * call that way.
		 *
		 * @throws std::logic_error if another dispatch() call is already in flight on this
		 * instance -- concurrently from another thread, or reentrantly from within @p worker
		 * itself.
		 * @throws Whatever the first batch to throw an exception threw, once every thread has
		 * finished. Other batches still run to completion regardless of one throwing.
		 */
		void dispatch(const Flattener<T>& flattener, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			// flattener/maxBatch/worker/allocated are instance members rather than local
			// state (unlike Dispatcher's Job), so a second concurrent or reentrant call on
			// this same instance would otherwise race on them -- reject it immediately,
			// before touching any of that state, rather than letting it corrupt things.
			throw_if<std::logic_error>(inProgress.exchange(true), "BatchDispatcher does not support concurrent dispatch() calls on the same instance");

			this->flattener = &flattener;
			this->maxBatch = maxBatch;
			this->worker = worker;
			allocated = 0;
			caughtException = nullptr;

			for(size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
				threads.emplace_back(&BatchDispatcher::workerThread, this);
			for(std::thread& workerThread : threads)
				workerThread.join();
			threads.clear();

			inProgress = false;

			// Surface the first exception thrown by any work unit to the caller of
			// dispatch(), rather than letting it escape the worker thread that hit it
			// (which would otherwise call std::terminate() and abort the whole process --
			// see workerThread()). Other work units still run to completion regardless.
			if(caughtException)
				std::rethrow_exception(caughtException);
		}

		/**
		 * @brief Returns the number of worker threads this BatchDispatcher spawns per
		 * dispatch() call.
		 *
		 * @return The worker thread count.
		 */
		unsigned int getThreadCount() { return threadCount; }

		/**
		 * @brief Worker thread count used when the constructor is given 0.
		 */
		constexpr static unsigned int defaultWorkerThreadCount = 4;

	private:
		void workerThread() {
			for(;;) {
				// Allocate work
				T start;
				unsigned int batchSize;
				{
					std::lock_guard<std::mutex> guard(mutex);
					const T todo = flattener->size() - allocated;
					if(todo == 0)
						return;

					start = allocated;
					batchSize = maxBatch;
					if(todo < maxBatch)
						batchSize = std::max<unsigned int>(1, static_cast<unsigned int>(todo / threadCount));
					allocated += batchSize;
				}

				// Do some work. Catch anything it throws rather than letting it escape
				// this thread's entry function -- an uncaught exception there would call
				// std::terminate() and abort the whole process. dispatch() rethrows the
				// first one it sees to its caller once every thread has finished instead.
				try {
					worker(*flattener, start, batchSize);
				} catch(...) {
					std::lock_guard<std::mutex> guard(mutex);
					if(!caughtException)
						caughtException = std::current_exception();
				}
			}
		}

		const unsigned int threadCount = 0;

		std::vector<std::thread> threads;
		std::mutex mutex;
		const Flattener<T>* flattener = nullptr;
		unsigned int maxBatch;
		std::function<void(const Flattener<T>&, T, unsigned int)> worker;

		T allocated = 0;

		// Guards against a second dispatch() call (concurrent or reentrant) running on
		// this instance while one is already in flight -- see dispatch().
		std::atomic<bool> inProgress = {false};

		// The first exception thrown by any work unit in the current dispatch() call, if
		// any; protected by mutex. Later ones are discarded -- work units still run to
		// completion regardless.
		std::exception_ptr caughtException;
	};
}
