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
#include <stdexcept>
#include <exception>
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
	 * @brief Runs a callback once for every point in an N-dimensional index space (described by
	 * a Flattener<>), spread across a persistent pool of worker threads.
	 *
	 * Only one dispatch() call may be in flight on a given instance at a time: dispatch() blocks
	 * the calling thread until every point in the space has been visited (or the Dispatcher is
	 * terminated), so a second call -- concurrent from another thread, or reentrant from within
	 * a worker callback -- is a usage error (see dispatch()). Put a thr::Queue in front of a
	 * Dispatcher if you need to queue up work submitted by multiple producer threads.
	 */
	class Dispatcher {
	public:
		/**
		 * @brief Constructs a Dispatcher with a fixed-size pool of worker threads, idle and
		 * waiting for work.
		 *
		 * @param threadCount Number of worker threads to start. 0 uses defaultWorkerThreadCount.
		 * Defaults to std::thread::hardware_concurrency().
		 */
		Dispatcher(unsigned int threadCount = std::thread::hardware_concurrency()) {
			this->threadCount = threadCount == 0 ? defaultWorkerThreadCount : threadCount;
			for(size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
				threads.emplace_back(&Dispatcher::workerThread, this);
		}

		/**
		 * @brief Destroys the Dispatcher, first calling terminate().
		 *
		 * @warning A thread that hasn't yet made its first dispatch() call on this instance must
		 * be joined (or otherwise known not to call dispatch() at all) before this runs --
		 * terminate() can only safely wait for a dispatch() call that's already in flight, not
		 * one it has no way of knowing is coming.
		 */
		virtual ~Dispatcher() {
			terminate();
		}

		/**
		 * @brief Stops this Dispatcher: no further work will be started, and any dispatch() call
		 * already in flight on this instance is given the chance to unwind safely before
		 * terminate() returns.
		 *
		 * Safe to call explicitly (it also runs automatically from ~Dispatcher()), safe to call
		 * more than once (idempotent), and safe to call concurrently with an in-flight dispatch()
		 * call from another thread -- that call either finishes the work already allocated to it
		 * or has its wait cut short once every worker thread is confirmed stopped, whichever
		 * comes first.
		 *
		 * @throws std::logic_error if called from within one of this Dispatcher's own worker
		 * callbacks (i.e. reentrantly, from a function passed to dispatch()) -- terminate() would
		 * otherwise need to join the very thread it's running on.
		 */
		void terminate() {
			// Serializes concurrent terminate() calls from different threads against each
			// other (they'd otherwise race on `threads` below), and against this thread
			// being one of the very worker threads terminate() is about to join() -- that
			// would either throw a self-join std::system_error partway through (if this
			// check didn't catch it first) or, worse, silently abandon the in-flight job's
			// remaining work units before completing, hanging the dispatch() call that's
			// waiting on this job forever. Reject it immediately, before touching any
			// state, so a rejected call has no side effects on the still-running job.
			std::lock_guard<std::mutex> terminateGuard(terminateMutex);
			for(const std::thread& workerThread : threads) {
				throw_if<std::logic_error>(workerThread.get_id() == std::this_thread::get_id(),
					"terminate() cannot be called from within one of this Dispatcher's own worker callbacks");
			}

			{
				std::lock_guard<std::mutex> guard(jobMutex);
				terminateAll = true;
			}

			// Wake up any sleeping threads.  They will check the terminate flag and
			// exit their run loop immediately upon waking.
			jobConditionVariable.notify_all();

			for(std::thread& workerThread : threads)
				workerThread.join();
			threads.clear();

			std::unique_lock<std::mutex> jobLock(jobMutex);
			// Every worker is confirmed done touching the current job (if any) now that
			// they're all joined, so it's safe for a dispatch() call still waiting on
			// this job to treat termination alone as license to stop waiting.
			allWorkersJoined = true;
			if(currentJob != nullptr)
				currentJob->conditionalVariable.notify_all();

			// Wait for any dispatch() call already in flight on this instance to finish
			// clearing currentJob before returning -- otherwise our caller (often
			// ~Dispatcher()) could destroy this object while that other thread still
			// expects jobMutex/currentJob to exist. A dispatch() call that hasn't
			// started yet by the time this returns is the caller's responsibility to
			// rule out (e.g. by joining any thread that might call dispatch() before
			// destroying this object), the same as with any other shared resource.
			jobConditionVariable.wait(jobLock, [this]{ return currentJob == nullptr; });
		}

		/**
		 * @brief Constructs a temporary Dispatcher and uses it for one dispatch() call over a
		 * 1-dimensional space of size @p count. Only useful for a single one-off dispatch, since
		 * the thread pool doesn't outlive this call.
		 *
		 * @param count The length of the (1-dimensional) parameter space.
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(const unsigned int count, const std::function<void(Flattener<>&, size_t)>& worker) {
			Dispatcher dispatcher;
			dispatcher.dispatch(count, worker);
		}

		/**
		 * @brief Constructs a temporary Dispatcher and uses it for one dispatch() call over the
		 * space described by @p shape. Only useful for a single one-off dispatch, since the
		 * thread pool doesn't outlive this call.
		 *
		 * @param shape The shape of the parameter space, outermost dimension first.
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(std::vector<unsigned int> shape, const std::function<void(Flattener<>&, size_t)>& worker) {
			Dispatcher dispatcher;
			dispatcher.dispatch(shape, worker);
		}

		/**
		 * @brief Constructs a temporary Dispatcher and uses it for one dispatch() call over
		 * @p dimensions. Only useful for a single one-off dispatch, since the thread pool
		 * doesn't outlive this call.
		 *
		 * @param dimensions The shape of the parameter space to cover.
		 * @param worker See dispatch().
		 * @throws Same as dispatch().
		 */
		static void defaultDispatch(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& worker) {
			Dispatcher dispatcher;
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
		 * @brief Runs @p worker once for every flat index in @p dimensions, spread across this
		 * Dispatcher's worker pool, blocking the calling thread until every index has been
		 * visited (or the Dispatcher is terminated).
		 *
		 * @param dimensions The shape of the parameter space to cover.
		 * @param worker Called as worker(dimensions, flatIndex) once per flat index. May run on
		 * any worker thread, and is never called concurrently for the same index, but different
		 * indices may run concurrently on different threads -- worker must be safe to call that
		 * way (e.g. distinct indices must not write to overlapping state without their own
		 * synchronization).
		 *
		 * @throws std::logic_error if another dispatch() call is already in flight on this
		 * instance -- concurrently from another thread, or reentrantly from within @p worker
		 * itself -- or if this Dispatcher has already been terminated (see terminate()).
		 * @throws Whatever the first work unit to throw an exception threw, once every work unit
		 * already handed out has finished running. Other work units still run to completion
		 * regardless of one throwing.
		 */
		void dispatch(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& worker) {
			// Dispatcher only ever runs one job at a time. dispatch() blocks the calling
			// thread until the job completes, so a second thread calling dispatch() on
			// the same Dispatcher concurrently, or any dispatch() call after terminate()
			// has run, is a caller usage error -- unsupported; put a thr::Queue in front
			// if you need to serialize dispatch() calls from multiple threads.
			Job* job;
			{
				std::lock_guard<std::mutex> guard(jobMutex);
				throw_if<std::logic_error>(terminateAll, "dispatch() called on a terminated Dispatcher");
				throw_if<std::logic_error>(currentJob != nullptr, "Dispatcher does not support concurrent dispatch() calls on the same instance");
				job = new Job(dimensions, worker);
				currentJob = job;
			}

			// Notify all worker threads that new work is available
			jobConditionVariable.notify_all();

			// Wait for the job to complete. allWorkersJoined (rather than terminateAll)
			// is the signal that it's safe to stop waiting on termination alone -- it's
			// only set after terminate() has confirmed every worker thread has actually
			// stopped touching this job, whereas terminateAll flips true well before that.
			std::exception_ptr caughtException;
			{
				std::unique_lock<std::mutex> jobCompletionLock(job->mutex);
				if(!job->complete() && !allWorkersJoined) {
					job->conditionalVariable.wait(jobCompletionLock, [job,this]{
						return job->complete() || allWorkersJoined;
					});
				}
				// Grab this while job is still alive; exception_ptr itself doesn't depend
				// on Job's lifetime once copied out, so it's safe to rethrow after delete.
				caughtException = job->exception;
			}

			{
				std::lock_guard<std::mutex> guard(jobMutex);
				currentJob = nullptr;
			}
			// Let a terminate() call that's waiting for this job to finish know it can proceed.
			jobConditionVariable.notify_all();

			delete job;

			// Surface the first exception thrown by any work unit to the caller of
			// dispatch(), rather than letting it escape the worker thread that hit it
			// (which would otherwise call std::terminate() and abort the whole process --
			// see workerThread()). Other work units still run to completion regardless.
			if(caughtException)
				std::rethrow_exception(caughtException);
		}

		/**
		 * @brief Returns the number of worker threads in this Dispatcher's pool. Reflects the
		 * count established at construction, even after terminate() has run.
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
						// All completed jobs must have been previously depleted -- there's no way
						// to complete a work unit that was never allocated.
						ensure(job->depleted());

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

				// Do some work. Catch anything it throws rather than letting it escape
				// this thread's entry function -- an uncaught exception there would call
				// std::terminate() and abort the whole process. dispatch() rethrows the
				// first one it sees to its caller once the job finishes instead.
				try {
					job->workFunction(job->dimensions, flatIndex);
				} catch(...) {
					std::lock_guard<std::mutex> guard(job->mutex);
					if(!job->exception)
						job->exception = std::current_exception();
				}
			}
		}

		class Job {
		private:
			Job(Flattener<> dimensions, const std::function<void(Flattener<>&, size_t)>& workFunction) : dimensions(dimensions), workFunction(workFunction) {}
			inline bool depleted() const { return allocated >= dimensions.size(); }
			inline bool complete() const  { return completed >= dimensions.size(); }

			Flattener<> dimensions;
			const std::function<void(Flattener<>&, size_t)> workFunction;

			size_t allocated = 0;	// How many work units have been allocated to threads, protected by Dispatcher::jobMutex
			size_t completed = 0;	// How many work units were completed, protected by Job::mutex

			// The first exception thrown by any work unit, if any; protected by mutex.
			// Later ones are discarded -- work units still run to completion regardless.
			std::exception_ptr exception;

			std::condition_variable conditionalVariable;
			std::mutex mutex;

			friend class Dispatcher;
		};

		unsigned int threadCount = 0;
		std::vector<std::thread> threads;
		std::atomic<bool> terminateAll = {false};

		// Set by terminate(), only after every worker thread is confirmed joined (and so
		// guaranteed done touching the current job, if any). Read across jobMutex/job->mutex
		// without a single consistent lock, hence atomic -- see dispatch()'s wait predicate.
		std::atomic<bool> allWorkersJoined = {false};

		// The job currently being run, or nullptr if this Dispatcher is idle.
		// Dispatcher only supports one job in flight at a time.
		Job* currentJob = nullptr;

		std::condition_variable jobConditionVariable;
		std::mutex jobMutex;

		// Serializes terminate() against concurrent calls to itself (see terminate()).
		std::mutex terminateMutex;
	};
};
