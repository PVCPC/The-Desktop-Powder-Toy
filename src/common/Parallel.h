#pragma once
#include <algorithm>
#include <atomic>
#include <execution>
#include <numeric>
#include <thread>
#include <vector>

// f(i) for i in [0, n), on all cores
template<class F>
void OverlayParallel(int n, F f)
{
#if defined(__cpp_lib_execution) && __cpp_lib_execution >= 201603L
	std::vector<int> ids(n);
	std::iota(ids.begin(), ids.end(), 0);
	std::for_each(std::execution::par, ids.begin(), ids.end(), f);
#else // Apple's libc++ has no parallel algorithms
	std::atomic<int> next = 0;
	auto work = [&] { for (int i; (i = next++) < n;) f(i); };
	std::vector<std::thread> threads(std::max(1u, std::thread::hardware_concurrency()) - 1);
	for (auto &t : threads) t = std::thread(work);
	work();
	for (auto &t : threads) t.join();
#endif
}
