# Dispatcher / BatchDispatcher / SlowDispatcher

Three interchangeable ways to run a callback over every point in an N-dimensional index space
(sized and addressed via [`Flattener<>`](https://github.com/lrmoorejr/flattener)), spread across a
thread pool. They share the same shape: give the dispatcher a shape and a worker function, and it
covers every point in that shape, blocking the caller until all of them are done -- `Dispatcher`
and `SlowDispatcher` call the worker once per point; `BatchDispatcher` calls it once per contiguous
batch of points (see below).

```cpp
#include "Dispatcher.hpp"

thr::Dispatcher dispatcher; // defaults to std::thread::hardware_concurrency() threads
dispatcher.dispatch({480, 640}, [](Flattener<>& shape, size_t flatIndex) {
    int row = shape.index(0, flatIndex);
    int col = shape.index(1, flatIndex);
    // ... process pixel (row, col) ...
});
// every pixel has been processed by the time dispatch() returns
```

## Which one do I want?

| | Use it when |
|---|---|
| **`Dispatcher`** | The default. Persistent thread pool, one callback per index. |
| **`BatchDispatcher`** | Each callback is so cheap that per-callback dispatch overhead dominates. Hands each thread a contiguous run of indices (`start`, `count`) per callback instead of one index at a time, so you amortize the overhead across a batch. |
| **`SlowDispatcher`** | Debugging, or a parameter space small enough that thread overhead isn't worth it. Single-threaded, same interface as `Dispatcher`, so it's a drop-in swap while stepping through a worker function. |

All three are constructed with an optional thread count (`SlowDispatcher` ignores it --
`getThreadCount()` always reports 1) and expose `dispatch(shape, worker)` plus a `defaultDispatch`
static that constructs a temporary instance for one-off use.

```cpp
// One-off dispatch without keeping a Dispatcher around
thr::Dispatcher::defaultDispatch(1000, [](Flattener<>& shape, size_t i) { /* ... */ });

// Swap in SlowDispatcher while debugging a worker function step by step
thr::SlowDispatcher dispatcher;
dispatcher.dispatch({480, 640}, worker);
```

### BatchDispatcher

`BatchDispatcher`'s worker takes a `(shape, start, count)` triple instead of a single index --
process `[start, start + count)`:

```cpp
thr::BatchDispatcher<> dispatcher;
dispatcher.dispatch(1'000'000, /* maxBatch */ 4096, [](const Flattener<>& shape, size_t start, unsigned int count) {
    for(unsigned int i = 0; i < count; ++i) {
        // process flat index start + i
    }
});
```

## Queueing

None of the three queue up `dispatch()` calls -- each instance only ever runs one job at a time.
For `Dispatcher` and `BatchDispatcher`, a second concurrent (or reentrant, e.g. calling `dispatch()`
again from inside a worker callback) `dispatch()` call on the same instance is a usage error and
throws `std::logic_error` rather than queueing or corrupting state. `SlowDispatcher` is the
exception -- concurrent `dispatch()` calls on the same instance are fine there, since each call
only ever touches its own local parameters, not shared state. If you need to queue up dispatch
work -- e.g. producer threads submitting jobs faster than a pool can drain them -- put a
[`thr::Queue`](https://github.com/lrmoorejr/queue) in front instead of expecting the dispatcher to
buffer it for you.

## Stopping early

`Dispatcher` and `SlowDispatcher` support cutting a `dispatch()` call short via `terminate()`,
callable from any thread (including from within the worker callback itself, to let a job decide
when it's done):

```cpp
thr::Dispatcher dispatcher;
std::thread stopper([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    dispatcher.terminate(); // any dispatch() call in flight unwinds safely
});
dispatcher.dispatch(1'000'000'000, worker); // returns early once terminate() is called
stopper.join();
```

Once `terminate()` has run, the instance is done -- a later `dispatch()` call throws
`std::logic_error` instead of running. `~Dispatcher()`/`~SlowDispatcher()` call `terminate()`
automatically. `BatchDispatcher` has no `terminate()` and needs none: it never keeps a thread pool
running past a `dispatch()` call returning, so there's nothing to cut short or clean up between
calls.

**Destroying a `Dispatcher` while another thread might still call `dispatch()` on it for the
first time is undefined behavior** -- the same contract as destroying a `std::mutex` someone's
about to lock. `terminate()` can only safely wait for a `dispatch()` call it already knows is in
flight; join any thread that might call `dispatch()` before destroying (or letting go out of
scope) the `Dispatcher` it's calling into. `SlowDispatcher` and `BatchDispatcher` don't have this
restriction, since neither one has a background thread that could still be mid-call when the
object is destroyed.

## Errors

All three throw `std::logic_error` for caller misuse (concurrent/reentrant `dispatch()` where
unsupported, or `dispatch()` after `terminate()`), and propagate whatever the worker callback
itself throws back out of `dispatch()`. For `Dispatcher`/`BatchDispatcher` that happens once every
already-started unit of work finishes -- other units keep running to completion regardless of one
of them throwing, since they're spread across threads. `SlowDispatcher` runs synchronously on the
caller's own thread, so a thrown exception stops the loop immediately instead, exactly like an
ordinary function call -- indices after the one that threw never run.
`BatchDispatcher::dispatch()` additionally throws `std::invalid_argument` if `maxBatch` is 0, and
`std::overflow_error` if a `count` passed to the 1-dimensional convenience overload doesn't fit in
an `unsigned int` (a single `Flattener<T>` dimension is always sized as one; for a larger space,
use the `Flattener<T>` overload with multiple dimensions instead).

## Requirements

- C++20 or later
- Header-only -- copy the header(s) you need into your project and `#include` them
- Links against threading support -- on Linux, compile/link with `-pthread` (or your build
  system's equivalent); `SlowDispatcher` is single-threaded and needs no threading support
- Requires [`Flattener.hpp`](https://github.com/lrmoorejr/flattener) on your include path --
  vendored here as a git submodule (`flattener/`); clone with `--recurse-submodules` or run
  `git submodule update --init` after cloning
- Optional (`Dispatcher` only): [`Ensure.hpp`](https://github.com/lrmoorejr/ensure) for a
  formatted diagnostic if an internal invariant is ever violated; falls back to plain `assert()`
  if not present. Not involved in any of the `std::logic_error`s under [Errors](#errors) above --
  those are always active regardless of `NDEBUG`, with or without `Ensure.hpp` on the include path.

## License

Apache License 2.0 -- see [LICENSE](LICENSE).
