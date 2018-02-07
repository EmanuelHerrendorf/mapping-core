
#include "operators/queryprofiler.h"
#include "util/exceptions.h"
#include "util/binarystream.h"

#include <unistd.h>
#include <time.h>
#include <limits>


ProfilingData::ProfilingData() : self_cpu(0), all_cpu(0), uncached_cpu(0),
	self_gpu(0), all_gpu(0), uncached_gpu(0), self_io(0), all_io(0), uncached_io(0) {
}

ProfilingData::ProfilingData(BinaryReadBuffer& buffer) :
	self_cpu(buffer.read<double>()),
	all_cpu(buffer.read<double>()),
	uncached_cpu(buffer.read<double>()),
	self_gpu(buffer.read<double>()),
	all_gpu(buffer.read<double>()),
	uncached_gpu(buffer.read<double>()),
	self_io(buffer.read<uint64_t>()),
	all_io(buffer.read<uint64_t>()),
	uncached_io(buffer.read<uint64_t>()) {
}

void ProfilingData::serialize(BinaryWriteBuffer &buffer, bool) const {
	buffer << self_cpu << all_cpu << uncached_cpu;
	buffer << self_gpu << all_gpu << uncached_gpu;
	buffer << self_io << all_io << uncached_io;
}

std::string ProfilingData::to_string() const {
	return concat(
			"CPU: [", self_cpu,",",all_cpu,",", uncached_cpu, "], ",
			"GPU: [", self_gpu,",",all_gpu,",", uncached_gpu, "], "
			"IO: [", self_io,",",all_io,",", uncached_io, "], "
	);
}


/*
 * QueryProfiler class
 */
QueryProfiler::QueryProfiler() : t_start(std::numeric_limits<double>::infinity()) {
}

double QueryProfiler::getTimestamp() {
#if defined(_POSIX_TIMERS) && defined(_POSIX_CPUTIME)
	struct timespec t;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0)
		throw OperatorException("QueryProfiler: clock_gettime() failed");
	return (double) t.tv_sec + t.tv_nsec/1000000000.0;
#else
	#warning "QueryProfiler: Cannot query CPU time on this OS, using wall time instead"

	struct timeval t;
	if (gettimeofday(&t, nullptr) != 0)
		throw OperatorException("QueryProfiler: gettimeofday() failed");
	return (double) t.tv_sec + t.tv_usec/1000000.0;
#endif
}

void QueryProfiler::startTimer() {
	if (t_start != std::numeric_limits<double>::infinity())
		throw OperatorException("QueryProfiler: Timer started twice");
	t_start = getTimestamp();
}

void QueryProfiler::stopTimer() {
	if (t_start == std::numeric_limits<double>::infinity())
		throw OperatorException("QueryProfiler: Timer not started");
	double cost = getTimestamp() - t_start;
	t_start = std::numeric_limits<double>::infinity();
	if (cost < 0)
		throw OperatorException("QueryProfiler: Timer stopped a negative time");
	self_cpu += cost;
	all_cpu += cost;
	uncached_cpu += cost;
}

void QueryProfiler::addGPUCost(double seconds) {
	self_gpu += seconds;
	all_gpu += seconds;
	uncached_gpu += seconds;
}

void QueryProfiler::addIOCost(size_t bytes) {
	self_io += bytes;
	all_io += bytes;
	uncached_io += bytes;
}

QueryProfiler& QueryProfiler::operator +=(const ProfilingData& other) {
	all_cpu += other.all_cpu;
	uncached_cpu += other.uncached_cpu;
	all_gpu += other.all_gpu;
	uncached_gpu += other.uncached_gpu;
	all_io += other.all_io;
	uncached_io += other.uncached_io;
	return *this;
}

QueryProfiler & QueryProfiler::operator+=(const QueryProfiler &other) {
	if (other.t_start != std::numeric_limits<double>::infinity())
		throw OperatorException("QueryProfiler: tried adding a timer that had not been stopped");
	return operator +=((ProfilingData&)other);
}

void QueryProfiler::cached(const ProfilingData &data) {
	uncached_cpu -= data.uncached_cpu;
	uncached_gpu -= data.uncached_gpu;
	uncached_io  -= data.uncached_io;
}

void QueryProfiler::addTotalCosts(const ProfilingData& profile) {
	all_cpu += profile.all_cpu;
	all_gpu += profile.all_gpu;
	all_io  += profile.all_io;
}
