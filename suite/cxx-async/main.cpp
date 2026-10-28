// std::async (on threads and deferred), std::promise and std::packaged_task.
//#ExpectedStdout: total=500017 task=42
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

int main()
{
	std::vector<std::future<long>> fs;
	for (int i = 0; i < 4; i++)
		fs.push_back(std::async(std::launch::async, [i] {
			long s = 0;
			for (int j = 0; j < 100000; j++)
				s += j % (i + 2);
			return s;
		}));
	auto deferred = std::async(std::launch::deferred, [] { return 7L; });
	std::promise<int> p;
	auto pf = p.get_future();
	std::thread t([&] { p.set_value(11); });
	long total = deferred.get() + pf.get();
	for (auto &f : fs)
		total += f.get();
	t.join();
	std::packaged_task<int(int)> task([](int x) { return x * 2; });
	auto tf = task.get_future();
	task(21);
	int r = tf.get();
	std::printf("total=%ld task=%d\n", total, r);
	return r == 42 ? 0 : 1;
}
