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
	 * @brief Similar to Dispatch in principle, but BatchDispatch sends blocks of data to the dispatch worker function.
	 * The intention is that this class is better at handling situations where each thread has a very small amount of work 
	 * to do.  In these circumstances, the overhead of Dispatch can outweigh the advantage of multithreaded computation.
	 * BatchDispatch solves this by allowing each thread to run batches of processing so that the overhead of BatchDispatch
	 * is negligeable by comparison.
	 * 
	 * @tparam T The type to use for indexing the space.  Must be large enough to index the entire space.
	 */
	template<typename T=size_t>
	class BatchDispatcher {
	public:
		/**
		 * @brief Construct a new BatchDispatcher object with the specified thead count.  If unspecified, the thread count
		 * used is based on the number of threads available on the system.  If 0, then defaultWorkerThreadCount threads will be reserved.
		 * 
		 * @param threadCount The number of threads to allocate to the dispatcher.
		 */
		BatchDispatcher(unsigned int threadCount = std::thread::hardware_concurrency()) 
			: threadCount(threadCount == 0 ? defaultWorkerThreadCount : threadCount) {}

		/**
		 * @brief Creates and executes a temporary dispatcher with a 1 dimensional space of size count, and a batch size of 1.
		 * The worker function will be called the flattened start index of its block, and count of how many indices to process.
		 * 
		 * @param count The length of the parameter space
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		static void defaultDispatch(T count, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			BatchDispatcher dispatcher;
			dispatcher.dispatch(count, 1, worker);
		}

		/**
		 * @brief Creates and executes a temporary dispatcher with a 1 dimensional space of size count and a specified batch size.
		 * The worker function will be called the flattened start index of its block, and count of how many indices to process.
		 * 
		 * @param count The length of the parameter space
		 * @param maxBatch The maximum size of the batch to send to each thread
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		static void defaultDispatch(T count, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			BatchDispatcher dispatcher;
			dispatcher.dispatch(count, maxBatch, worker);
		}

		/**
		 * @brief Creates and executes a temporary dispatcher with a shape specified by the supplied vector of dimensions and a specified batch size.
		 * The worker function will be called the flattened start index of its block, and count of how many indices to process.
		 * 
		 * @param shape The shape of the parameter space expressed in dimensions
		 * @param maxBatch The maximum size of the batch to send to each thread
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		static void defaultDispatch(const std::vector<unsigned int>& shape, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			BatchDispatcher dispatcher;
			dispatcher.dispatch(shape, maxBatch, worker);
		}

		/**
		 * @brief Creates and executes a temporary dispatcher with the shape defined in a Flattener object and a specified batch size
		 * The worker function will be called the flattened start index of its block, and count of how many indices to process.
		 * 
		 * @param maxBatch The maximum size of the batch to send to each thread
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		static void defaultDispatch(const Flattener<T>& flattener, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			BatchDispatcher dispatcher;
			dispatcher.dispatch(flattener, maxBatch, worker);
		}

		/**
		 * @brief Executes the dispatcher (ie processes a space) with a 1 dimensional space of size count, and a batch size of 1.
		 * The worker function will be called the flattened start index of its block, and count of how many indices to process.
		 * 
		 * @param count The length of the parameter space
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		void dispatch(T count, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			dispatch(Flattener<T>({static_cast<unsigned int>(count)}), maxBatch, worker);
		}

		/**
		 * @brief Executes the dispatcher (ie processes a space) with a shape specified by the supplied initializer list of dimensions and a specified batch size.
		 * The worker function will be called the flattened start index of its block, and count of how many indices to process.
		 * 
		 * @param shape The shape of the parameter space expressed in dimensions
		 * @param maxBatch The maximum size of the batch to send to each thread
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		void dispatch(std::initializer_list<unsigned int> shape, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			dispatch(Flattener<T>(shape), maxBatch, worker);
		}

		/**
		 * @brief Executes the dispatcher (ie processes a space) with a shape specified by the supplied vector of dimensions and a specified batch size.
		 * The worker function will be called the flattened start index of its block, and count of how many indices to process.
		 * 
		 * @param shape The shape of the parameter space expressed in dimensions
		 * @param maxBatch The maximum size of the batch to send to each thread
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		void dispatch(std::vector<unsigned int>& shape, unsigned int maxBatch, const std::function<void(const Flattener<T>&, T, unsigned int)>& worker) {
			dispatch(Flattener<T>(shape), maxBatch, worker);
		}

		/**
		 * @brief Executes the dispatcher (ie processes a space) with the shape defined in a Flattener object and a specified batch size
		 * The worker function will be called the flattened start index of its block, and count of how many indices to process.
		 * 
		 * @param maxBatch The maximum size of the batch to send to each thread
		 * @param worker The worker that the dispatcher will call to process the space
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
		 * @brief Returns the size of the thread pool for this Dispatcher
		 * 
		 * @return unsigned int The thread count
		 */
		unsigned int getThreadCount() { return threadCount; }

		/**
		 * @brief Default thread count when 0 is specified
		 * 
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
