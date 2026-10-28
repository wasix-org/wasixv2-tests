// std::thread, std::mutex, std::condition_variable and atomic wait/notify (libc++ waits on a
// futex).
//#BuildEnv: WASIXCC_COMPILER_FLAGS_CXX=-std=c++20
//#ExpectedStdout: counter=40000 threads=4
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

int main()
{
	std::mutex m;
	std::condition_variable cv;
	long counter = 0;
	std::atomic<int> done{0};
	std::vector<std::thread> ts;
	for (int i = 0; i < 4; i++)
		ts.emplace_back([&] {
			for (int j = 0; j < 10000; j++) {
				std::lock_guard<std::mutex> g(m);
				counter++;
			}
			done.fetch_add(1);
			done.notify_all();
			cv.notify_one();
		});
	{
		std::unique_lock<std::mutex> lk(m);
		cv.wait(lk, [&] { return done.load() == 4; });
	}
	for (auto &t : ts)
		t.join();
	int d = done.load();
	done.wait(d + 1); // the value differs: returns at once
	std::printf("counter=%ld threads=%d\n", counter, d);
	return counter == 40000 && std::thread::hardware_concurrency() > 0 ? 0 : 1;
}
