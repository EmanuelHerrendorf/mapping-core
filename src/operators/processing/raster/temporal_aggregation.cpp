#include "datatypes/raster.h"
#include "datatypes/raster/typejuggling.h"
#include "raster/opencl.h"
#include "operators/operator.h"
#include "util/formula.h"
#include "util/enumconverter.h"

#include <limits>
#include <memory>
#include <sstream>
#include <json/json.h>
#include <iostream>

enum class AggregationType {
	MIN, MAX, AVG
};

const std::vector<std::pair<AggregationType, std::string> > AggregationTypeMap {
		std::make_pair(AggregationType::MIN, "min"), std::make_pair(
				AggregationType::MAX, "max"), std::make_pair(
				AggregationType::AVG, "avg") };

static EnumConverter<AggregationType> AggregationTypeConverter(
		AggregationTypeMap);

/**
 * Operator that aggregates a given input raster over a given time interval
 *
 * Parameters:
 * - duration: the length of the time interval in seconds as double
 * - aggregation: "min", "max", "avg"
 */
class TemporalAggregationOperator: public GenericOperator {
public:
	TemporalAggregationOperator(int sourcecounts[], GenericOperator *sources[],
			Json::Value &params);
	virtual ~TemporalAggregationOperator();

#ifndef MAPPING_OPERATOR_STUBS
	std::unique_ptr<Raster2D<double>> createAccumulator(GenericRaster &raster);
	virtual std::unique_ptr<GenericRaster> getRaster(const QueryRectangle &rect,
			const QueryTools &tools);
#endif
protected:
	void writeSemanticParameters(std::ostringstream& stream);
private:
	double duration;
	AggregationType aggregationType;

	std::unique_ptr<GenericRaster>
	sampleAggregation(std::unique_ptr<GenericRaster> unique_ptr, const QueryRectangle &rectangle, const QueryTools &tools);
};

TemporalAggregationOperator::TemporalAggregationOperator(int sourcecounts[],
		GenericOperator *sources[], Json::Value &params) :
		GenericOperator(sourcecounts, sources) {
	assumeSources(1);
	if (!params.isMember("duration")) {
		throw OperatorException(
				"TemporalAggregationOperator: Parameter duration is missing");
	}
	duration = params.get("duration", 0).asDouble();

	if (!params.isMember("aggregation")) {
		throw OperatorException(
				"TemporalAggregationOperator: Parameter aggregation is missing");
	}
	aggregationType = AggregationTypeConverter.from_string(
			params.get("aggregation", "").asString());
}

TemporalAggregationOperator::~TemporalAggregationOperator() {
}
REGISTER_OPERATOR(TemporalAggregationOperator, "temporal_aggregation");

void TemporalAggregationOperator::writeSemanticParameters(
		std::ostringstream& stream) {
	Json::Value json(Json::objectValue);
	json["duration"] = duration;
	json["aggregation"] = AggregationTypeConverter.to_string(aggregationType);

	stream << json;
}

#ifndef MAPPING_OPERATOR_STUBS

template<typename T>
struct Accumulate {
	static void execute(Raster2D<T> *raster, Raster2D<double> *accumulator, AggregationType aggregationType) {
		raster->setRepresentation(GenericRaster::Representation::CPU);
		// accumulate
		for (int x = 0; x < raster->width; ++x) {
			for (int y = 0; y < raster->height; ++y) {
				T rasterValue = raster->get(x, y);
				double accValue = accumulator->get(x, y);
				double newValue;

				if (raster->dd.is_no_data(rasterValue) || std::isnan(accValue)) {
					newValue = NAN;
				} else {

					switch (aggregationType) {
					case AggregationType::MIN:
						newValue = std::min(static_cast<double>(rasterValue), accValue);
						break;
					case AggregationType::MAX:
						newValue = std::max(static_cast<double>(rasterValue), accValue);
						break;
					case AggregationType::AVG:
						newValue = static_cast<double>(rasterValue) + accValue;
						break;
					}
				}
				accumulator->set(x, y, newValue);
			}
		}

	}
};

template<typename T>
struct Output {
	static std::unique_ptr<GenericRaster> execute(Raster2D<T> *raster, Raster2D<double> *accumulator,
			AggregationType aggregationType, size_t n) {
		auto output = std::make_unique<Raster2D<T>>(raster->dd, accumulator->stref,
											   accumulator->width, accumulator->height);

		for (int x = 0; x < raster->width; ++x) {
			for (int y = 0; y < raster->height; ++y) {
				double accValue = accumulator->get(x, y);
				T outputValue;

				if (std::isnan(accValue)) {
					if (!raster->dd.has_no_data) {
						throw OperatorException(
								"Temporal_Aggregation: No data value in data without no data value");
					} else {
						outputValue = raster->dd.no_data;
					}
				} else {
					switch (aggregationType) {
					case AggregationType::MIN:
						outputValue = static_cast<T>(accValue);
						break;
					case AggregationType::MAX:
						outputValue = static_cast<T>(accValue);
						break;
					case AggregationType::AVG:
						// TODO: solve for non-equi length time validities
						outputValue = static_cast<T>(accValue / n);
						break;
					}
				}
				output->set(x, y, outputValue);
			}
		}

		return std::unique_ptr<GenericRaster>(output.release());
	}
};

std::unique_ptr<Raster2D<double>> TemporalAggregationOperator::createAccumulator(
		GenericRaster &raster) {
	DataDescription accumulatorDD = raster.dd;
	accumulatorDD.datatype = GDT_Float64;

	auto accumulator = std::make_unique<Raster2D<double>>(accumulatorDD,
			raster.stref, raster.width, raster.height);

	// initialize accumulator
	for (int x = 0; x < accumulator->width; ++x) {
		for (int y = 0; y < accumulator->height; ++y) {
			accumulator->set(x, y, raster.getAsDouble(x, y));
		}
	}

	return accumulator;
}

std::unique_ptr<GenericRaster> TemporalAggregationOperator::getRaster(
		const QueryRectangle &rect, const QueryTools &tools) {
	// TODO: compute using OpenCL
	// TODO: allow LOOSE computations

	auto input = getRasterFromSource(0, rect, tools, RasterQM::EXACT);

	if (input->stref.t2 == input->stref.t1 + input->stref.epsilon()) {
		// TODO: refactor when raster time series are introduced
		return sampleAggregation(std::move(input), rect, tools);
	}

	auto accumulator = createAccumulator(*input);

	size_t n = 1;
	QueryRectangle nextRect = rect;
	nextRect.t1 = input->stref.t2;
	nextRect.t2 = nextRect.t1 + nextRect.epsilon();

	// TODO: what to do with rasters that are partially contained in timespan?
    // TODO: gaps in rasters temporal validity
	while (nextRect.t1 < rect.t1 + duration) {
		std::unique_ptr<GenericRaster> rasterFromSource = getRasterFromSource(0, nextRect, tools,
					RasterQM::EXACT);

		// accumulate
		callUnaryOperatorFunc<Accumulate>(rasterFromSource.get(), accumulator.get(), aggregationType);

		nextRect.t1 = rasterFromSource->stref.t2;
		nextRect.t2 = nextRect.t1 + nextRect.epsilon();
		n += 1;
	}

	return callUnaryOperatorFunc<Output>(input.get(), accumulator.get(), aggregationType, n);
}

std::unique_ptr<GenericRaster>
TemporalAggregationOperator::sampleAggregation(std::unique_ptr<GenericRaster> input, const QueryRectangle &rect, const QueryTools &tools) {
	const size_t n = 3; // TODO: introduce (optional) parameter
	double timeDelta = (rect.t2 - rect.t1) / n;

	auto accumulator = createAccumulator(*input);

	QueryRectangle nextRect = rect;
	nextRect.t1 = input->stref.t1 + timeDelta;
	nextRect.t2 = nextRect.t1 + nextRect.epsilon();

	for (size_t i = 0; i < n; ++i) {
		std::unique_ptr<GenericRaster> rasterFromSource = getRasterFromSource(0, nextRect, tools,
																			  RasterQM::EXACT);

		// accumulate
		callUnaryOperatorFunc<Accumulate>(rasterFromSource.get(), accumulator.get(), aggregationType);

		nextRect.t1 = rasterFromSource->stref.t1 + timeDelta;
		nextRect.t2 = nextRect.t1 + nextRect.epsilon();
	}

	return callUnaryOperatorFunc<Output>(input.get(), accumulator.get(), aggregationType, n);
}

#endif

