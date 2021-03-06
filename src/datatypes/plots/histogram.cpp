
#include "util/exceptions.h"
#include "datatypes/plots/histogram.h"

#include <cmath>
#include <limits>


Histogram::Histogram(unsigned long number_of_buckets, double min, double max)
        : counts(number_of_buckets), nodata_count(0), min(min), max(max) {

    if (!std::isfinite(min)) {
        throw ArgumentException("Histogram: min is not finite");
    } else if (!std::isfinite(max)) {
        throw ArgumentException("Histogram: max is not finite");
    } else if (min == max && number_of_buckets > 1) {
        throw ArgumentException("Histogram: number_of_buckets must be 1 if min = max");
    } else if (min > max) {
        throw ArgumentException(concat("Histogram: min > max (", min, " > ", max, ")"));
    }
}

Histogram::~Histogram() = default;

void Histogram::inc(double value) {
    if (value < min || value > max) {
        incNoData();
        return;
    }

    counts[calculateBucketForValue(value)]++;
}

int Histogram::calculateBucketForValue(double value) {
    if (max > min) {
        auto bucket = static_cast<int>(std::floor(((value - min) / (max - min)) * counts.size()));
        bucket = std::min((int) counts.size() - 1, std::max(0, bucket));
        return bucket;
    } else {
        return 0; // `number_of_buckets` is 1 if `min = max`
    }
}

double Histogram::calculateBucketLowerBorder(int bucket) {
    return (bucket * ((max - min) / counts.size())) + min;
}

void Histogram::incNoData() {
    nodata_count++;
}

int Histogram::getValidDataCount() {
    //return std::accumulate(counts.begin(), counts.end(), 0);
    int sum = 0;
    for (int &i: counts) {
        sum += i;
    }
    return sum;
}

void Histogram::addMarker(double bucket, const std::string &label) {
    markers.emplace_back(bucket, label);
}

const std::string Histogram::toJSON() const {
    std::stringstream buffer;
    buffer << R"({"type": "histogram", )";
    buffer << R"("metadata": {"min": )" << min << ", \"max\": " << max << ", \"nodata\": " << nodata_count
           << ", \"numberOfBuckets\": " << counts.size() << "}, ";
    buffer << "\"data\": [";
    for (size_t i = 0; i < counts.size(); i++) {
        if (i != 0)
            buffer << " ,";
        buffer << counts.at(i);
    }
    buffer << "] ";
    if (!markers.empty()) {
        buffer << ", ";
        buffer << "\"lines\":[";

        for (size_t i = 0; i < markers.size(); i++) {
            if (i != 0)
                buffer << " ,";
            auto marker = markers.at(i);
            buffer << R"({"name":")" << marker.second << R"(" ,"pos":)" << std::to_string(marker.first) << "}";
        }
        buffer << "]";
    }
    buffer << "} ";
    return buffer.str();
}

std::unique_ptr<GenericPlot> Histogram::clone() const {
    auto copy = std::make_unique<Histogram>(counts.size(), min, max);

    copy->nodata_count = nodata_count;
    copy->markers = markers;
    copy->counts = counts;

    return std::unique_ptr<GenericPlot>(copy.release());
}

Histogram::Histogram(BinaryReadBuffer &buffer) {
    buffer.read(&counts);
    buffer.read(&nodata_count);
    buffer.read(&min);
    buffer.read(&max);

    auto count = buffer.read<size_t>();
    for (size_t i = 0; i < count; i++) {
        auto p1 = buffer.read<double>();
        std::string p2 = buffer.read<std::string>();
        markers.emplace_back(p1, p2);
    }
}

void Histogram::serialize(BinaryWriteBuffer &buffer, bool is_persistent_memory) const {
    buffer << Type::Histogram;

    buffer << counts;
    buffer << nodata_count;
    buffer << min;
    buffer << max;

    size_t count = markers.size();
    buffer.write(count);
    for (auto &e : markers) {
        buffer.write(e.first);
        buffer.write(e.second);
    }
}
