// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bench.h"

#include <iostream>

using namespace benchmark;

std::map<std::string, BenchFunction> BenchRunner::benchmarks;



#if defined(HAVE_CONFIG_H)

#include <Windows.h>
#include <stdint.h> // portable: uint64_t   MSVC: __int64 


int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
	// Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
	// This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
	// until 00:00:00 January 1, 1970 
	static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

	SYSTEMTIME  system_time;
	FILETIME    file_time;
	uint64_t    time;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	time = ((uint64_t)file_time.dwLowDateTime);
	time += ((uint64_t)file_time.dwHighDateTime) << 32;

	tp->tv_sec = (long)((time - EPOCH) / 10000000L);
	tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
	return 0;
}

#else

#include <sys/time.h>

#endif // HAVE_CONFIG_H



static double gettimedouble(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_usec * 0.000001 + tv.tv_sec;
}

BenchRunner::BenchRunner(std::string name, BenchFunction func)
{
    benchmarks.insert(std::make_pair(name, func));
}

void
BenchRunner::RunAll(double elapsedTimeForOne)
{
    std::cout << "Benchmark" << "," << "count" << "," << "min" << "," << "max" << "," << "average" << "\n";

    for (std::map<std::string,BenchFunction>::iterator it = benchmarks.begin();
         it != benchmarks.end(); ++it) {

        State state(it->first, elapsedTimeForOne);
        BenchFunction& func = it->second;
        func(state);
    }
}

bool State::KeepRunning()
{
    double now;
    if (count == 0) {
        beginTime = now = gettimedouble();
    }
    else {
        // timeCheckCount is used to avoid calling gettime most of the time,
        // so benchmarks that run very quickly get consistent results.
        if ((count+1)%timeCheckCount != 0) {
            ++count;
            return true; // keep going
        }
        now = gettimedouble();
        double elapsedOne = (now - lastTime)/timeCheckCount;
        if (elapsedOne < minTime) minTime = elapsedOne;
        if (elapsedOne > maxTime) maxTime = elapsedOne;
        if (elapsedOne*timeCheckCount < maxElapsed/16) timeCheckCount *= 2;
    }
    lastTime = now;
    ++count;

    if (now - beginTime < maxElapsed) return true; // Keep going

    --count;

    // Output results
    double average = (now-beginTime)/count;
    std::cout << name << "," << count << "," << minTime << "," << maxTime << "," << average << "\n";

    return false;
}
