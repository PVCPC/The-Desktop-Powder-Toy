#pragma once
#include <algorithm>
#include <atomic>
#include <execution>
#include <numeric>
#include <thread>
#include <vector>
#ifdef __APPLE__
# include <dispatch/dispatch.h>
#endif

// Multicore drawing (Overlay.FastDraw, the Simulation tab): the overlay's per-pixel passes and TPT's fire glow on all
// cores, skipping what's empty. The same pixels and sim either way: off is one core, as before.
inline bool overlayFastDraw = true;

// f(i) for i in [0, n), on all cores
template<class F>
void OverlayParallel(int n, F f)
{
#if defined(__cpp_lib_execution) && __cpp_lib_execution >= 201603L
	std::vector<int> ids(n);
	std::iota(ids.begin(), ids.end(), 0);
	std::for_each(std::execution::par, ids.begin(), ids.end(), f);
#elif defined(__APPLE__) // no parallel algorithms in its libc++; GCD's pool, not a new set of threads per call
	dispatch_apply_f(size_t(n), DISPATCH_APPLY_AUTO, &f, [](void *context, size_t i) { (*static_cast<F *>(context))(int(i)); });
#else
	std::atomic<int> next = 0;
	auto work = [&] { for (int i; (i = next++) < n;) f(i); };
	std::vector<std::thread> threads(std::max(1u, std::thread::hardware_concurrency()) - 1);
	for (auto &t : threads) t = std::thread(work);
	work();
	for (auto &t : threads) t.join();
#endif
}
