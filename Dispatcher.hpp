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

#include <cstdlib>
#include <functional>
#include <thread>
#include <condition_variable>
#include <vector>
#include <atomic>
#include "Flattener.hpp"

// Ensure.hpp is an optional dependency: if it's available (either as part of this
// checkout or vendored alongside this header), use its ensure() for a formatted
// diagnostic on failure; otherwise fall back to plain assert() so this header still
// works standalone.
#if __has_include("commons/Ensure.hpp")
	#include "commons/Ensure.hpp"
#elif __has_include("Ensure.hpp")
	#include "Ensure.hpp"
#else
	#include <cassert>
	// Guard against Ensure.hpp having already been included under a path our
	// __has_include checks above don't know about (e.g. vendored elsewhere as
	// "3rdparty/Ensure.hpp") -- COMMONS_ENSURE_HPP is defined by Ensure.hpp
	// itself, so this still catches that case even under an unknown filename.
	#if !defined(COMMONS_ENSURE_HPP) && !defined(ensure)
		#define ensure(condition, ...) assert((condition))
	#endif
#endif

namespace thr {
	/**
	 * @brief Dispatcher manages a pool of threads that can be applied to a tensor space. The tensor
	 * dimensions can be expressed a number of ways, but ultimately they will be translated into
	 * a Flattener object, which allows tanslation between dimensional indices and a linear 
	 * chunk of memory.
	 * 
	 */
	class Dispatcher {
	public:
		/**
		 * @brief Construct a new Dispatcher object with the given number of threads allocated, waiting for work.
		 * 
		 * @param threadCount Number of threads to allocate for this Dispatcher.  0 will use the defaultWorkerThreadCount
		 */
		Dispatcher(unsigned int threadCount = std::thread::hardware_concurrency()) {
			this->threadCount = threadCount == 0 ? defaultWorkerThreadCount : threadCount;
			for(size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
				threads.emplace_back(&Dispatcher::workerThread, this);
		}
		virtual ~Dispatcher() {
			terminate();
		}

		/**
		 * Terminate all activities in this Dispatcher.
		 */
		void terminate() {
			std::unique_lock<std::mutex> jobLock(jobMutex);
			terminateAll = true;
			jobLock.unlock();

			// Wake up any sleeping threads.  They will check the terminate flag and
			// exit their run loop immediately upon waking.
			jobConditionVariable.notify_all();

			for(std::thread& workerThread : threads)
				workerThread.join();
			threads.clear();

			// If a dispatch() call is still waiting on a job that never reached
			// completion, wake it so it can clean up.
			if(currentJob != nullptr)
				currentJob->conditionalVariable.notify_all();
		}

		/**
		 * @brief Creates and executes a temporary dispatcher with a 1 dimensional space of size count
		 * 
		 * @param count The length of the parameter space
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		static void defaultDispatch(const unsigned int count, const std::function<void(Flattener<>&, size_t)>& worker) {
			Dispatcher dispatcher;
			dispatcher.dispatch(count, worker);
		}

		/**
		 * @brief Creates and executes a temporary dispatcher with a shape specified by the supplied vector of dimensions
		 * 
		 * @param shape The shape of the parameter space expressed in dimensions
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		static void defaultDispatch(std::vector<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			Dispatcher dispatcher;
			dispatcher.dispatch(shape, worker);
		}

		/**
		 * @brief Creates and executes a temporary dispatcher with the shape defined in a Flattener object
		 * 
		 * @param dimensions THe shape of the parameter space
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		static void defaultDispatch(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& worker) {
			Dispatcher dispatcher;
			dispatcher.dispatch(dimensions, worker);
		}

		/**
		 * @brief Executes the dispatcher (ie processes a space) with a 1 dimensional space of size count
		 * 
		 * @param count The length of the parameter space
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		void dispatch(const unsigned int count, const std::function<void(Flattener<>&, size_t)>& worker) {
			dispatch({count}, worker);
		}

		/**
		 * @brief Executes the dispatcher (ie processes a space) with a shape specified by an intializer_list of dimensions
		 * 
		 * @param shape The shape of the parameter space expressed in dimensions
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		void dispatch(std::initializer_list<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			dispatch(Flattener<>(shape), worker);
		}

		/**
		 * @brief Executes the dispatcher (ie processes a space) with a shape specified by the supplied vector of dimensions
		 * 
		 * @param shape The shape of the parameter space expressed in dimensions
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		void dispatch(std::vector<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			dispatch(Flattener<>(shape), worker);
		}

		/**
		 * @brief Executes the dispatcher (ie processes a space) with the shape defined in a Flattener object
		 * 
		 * @param dimensions THe shape of the parameter space
		 * @param worker The worker that the dispatcher will call to process the space
		 */
		void dispatch(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& worker) {
			// Construct a new job
			Job* job = new Job(dimensions, worker);

			// Dispatcher only ever runs one job at a time. dispatch() blocks the calling
			// thread until the job completes, so this only fires if a second thread calls
			// dispatch() on the same Dispatcher concurrently -- unsupported; put a
			// thr::Queue in front if you need to serialize dispatch() calls from multiple
			// threads.
			std::unique_lock<std::mutex> jobLock(jobMutex);
			ensure(currentJob == nullptr, "Dispatcher does not support concurrent dispatch() calls on the same instance");
			currentJob = job;
			jobLock.unlock();

			// Notify all worker threads that new work is available
			jobConditionVariable.notify_all();

			// Wait for the job to complete
			std::unique_lock<std::mutex> jobCompletionLock(job->mutex);
			if(!job->complete() && !terminateAll) {
				job->conditionalVariable.wait(jobCompletionLock, [job,this]{
					return job->complete() || terminateAll;
				});
			}
			jobCompletionLock.unlock();

			jobLock.lock();
			currentJob = nullptr;
			jobLock.unlock();

			delete job;
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
			Job* job = nullptr;
			while(true) {
				// Lock so we can wait if needed, and manipulate the current job
				std::unique_lock<std::mutex> jobLock(jobMutex);

				// if job is non-null, then we just completed a work unit from it.
				if(job != nullptr) {
					std::lock_guard<std::mutex> guard(job->mutex);

					// Make note of progress and wrap up the job if everything is complete.
					// A lock isn't needed because we have jobMutex locked
					job->completed++;

					// Is this job complete?
					if(job->complete()) {
						// Notify dispatch() that it can clean up the Job.
						job->conditionalVariable.notify_all();

						// !!! Cannot use job reference anymore because dispatch may have deleted it
					}
				}

				// Wait until there is a current job with unallocated work, or we're terminating.
				// Do not initiate a wait if the terminate flag has already been set, because the
				// notify_all() may already have been called from terminate() by the time we get here.
				if((currentJob == nullptr || currentJob->depleted()) && !terminateAll)
					jobConditionVariable.wait(jobLock, [this]{return (currentJob != nullptr && !currentJob->depleted()) || terminateAll;});
				if(terminateAll)
					break;

				job = currentJob;

				// Grab some work from this job before unlocking.
				size_t flatIndex = job->allocated++;

				jobLock.unlock();

				// Do some work
				job->workFunction(job->dimensions, flatIndex);
			}
		}

		class Job {
		private:
			Job(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& workFunction) : dimensions(dimensions), workFunction(workFunction) {}
			inline bool depleted() const { return allocated >= dimensions.size(); }
			inline bool complete() const  { return completed >= dimensions.size(); }

			Flattener<> dimensions;
			const std::function<void(Flattener<>&, size_t)> workFunction;

			std::atomic<size_t> allocated = {0};	// How many work units have been allocated to threads, protected by Dispatcher::jobMutex
			std::atomic<size_t> completed = {0};	// How many work units were completed, protected by Job::mutex

			std::condition_variable conditionalVariable;
			std::mutex mutex;

			friend class Dispatcher;
		};

		unsigned int threadCount = 0;
		std::vector<std::thread> threads;
		std::atomic<bool> terminateAll = {false};

		// The job currently being run, or nullptr if this Dispatcher is idle.
		// Dispatcher only supports one job in flight at a time.
		Job* currentJob = nullptr;

		std::condition_variable jobConditionVariable;
		std::mutex jobMutex;
	};
};
