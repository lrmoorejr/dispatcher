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

None of the three queue up `dispatch()` calls -- each instance only ever runs one job at a time
(a second concurrent `dispatch()` call on the same instance is a usage error, checked via
`ensure()` in `Dispatcher`). If you need to queue dispatch work -- e.g. producer threads that
submit jobs faster than a pool can drain them -- put a
[`thr::Queue`](https://github.com/lrmoorejr/queue) in front instead of expecting the dispatcher to
buffer it for you.

## Requirements

- C++20 or later
- Header-only -- copy the header(s) you need into your project and `#include` them
- Links against threading support -- on Linux, compile/link with `-pthread` (or your build
  system's equivalent); `SlowDispatcher` is single-threaded and needs no threading support
- Requires [`Flattener.hpp`](https://github.com/lrmoorejr/flattener) on your include path --
  vendored here as a git submodule (`flattener/`); clone with `--recurse-submodules` or run
  `git submodule update --init` after cloning
- Optional (`Dispatcher` only): [`Ensure.hpp`](https://github.com/lrmoorejr/ensure) for a
  formatted diagnostic when `dispatch()` is called concurrently on the same instance; falls back
  to plain `assert()` if not present

## License

Apache License 2.0 -- see [LICENSE](LICENSE).
