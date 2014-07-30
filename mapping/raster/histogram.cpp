
#include "raster/exceptions.h"
#include "raster/histogram.h"

#include <cstdio>
#include <cmath>
#include <limits>


Histogram::Histogram(int number_of_buckets, double min, double max)
	: counts(number_of_buckets), nodata_count(0), min(min), max(max) {

	if (!std::isfinite(min) || !std::isfinite(max))
		throw ArgumentException("Histogram: min or max not finite");
	if (min >= max)
		throw ArgumentException("Histogram: min >= max");
}


Histogram::~Histogram() {

}


void Histogram::print() {
	printf("{\"min\": %f, \"max\": %f, \"nodata\": %d, \"buckets\": [", min, max, nodata_count);
	for (auto it = counts.begin(); it != counts.end(); it++) {
		if (it != counts.begin())
			printf(",");
		printf("%d", *it);
	}
	printf("]}");
}


void Histogram::inc(double value) {
	if (value < min || value > max) {
		incNoData();
		return;
	}

	int bucket = std::floor(((value - min) / (max - min)) * counts.size());
	bucket = std::min((int) counts.size()-1, std::max(0, bucket));
	counts[bucket]++;
}


void Histogram::incNoData() {
	nodata_count++;
}
