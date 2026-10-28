// std::chrono's clocks and C++20 calendar types, and sleep_for.
//#BuildEnv: WASIXCC_COMPILER_FLAGS_CXX=-std=c++20
//#ExpectedStdout: slept ok
//#ExpectedStdout: calendar ok
#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

using namespace std::chrono;

int main()
{
	auto t0 = steady_clock::now();
	std::this_thread::sleep_for(milliseconds(50));
	auto ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();
	if (ms < 50 || ms >= 5000) {
		std::printf("slept %lld ms\n", (long long)ms);
		return 1;
	}
	std::printf("slept ok\n");
	auto now = system_clock::now();
	year_month_day ymd{floor<days>(now)};
	std::time_t t = system_clock::to_time_t(now);
	std::tm tm;
	gmtime_r(&t, &tm);
	if ((int)ymd.year() != tm.tm_year + 1900 || (unsigned)ymd.month() != (unsigned)tm.tm_mon + 1 ||
	    (unsigned)ymd.day() != (unsigned)tm.tm_mday || (int)ymd.year() < 2024 ||
	    !high_resolution_clock::is_steady) {
		std::printf("calendar %d-%u-%u\n", (int)ymd.year(), (unsigned)ymd.month(),
			    (unsigned)ymd.day());
		return 1;
	}
	std::printf("calendar ok\n");
	return 0;
}
