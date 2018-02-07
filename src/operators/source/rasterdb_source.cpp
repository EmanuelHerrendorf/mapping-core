
#include "datatypes/raster.h"
#include "datatypes/raster/typejuggling.h"
#include "rasterdb/rasterdb.h"
#include "raster/opencl.h"
#include "operators/operator.h"
#include "util/configuration.h"

#include <memory>
#include <sstream>
#include <cmath>
#include <json/json.h>


/**
 * Operator that gets raster data from the rasterdb
 *
 * Parameters:
 * - sourcename: the name of the datasource
 * - channel: the index of the requested channel
 * - transform: true, if the specified transform function in the datasource should be applied
 */
class RasterDBSourceOperator : public GenericOperator {
	public:
		RasterDBSourceOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params);
		virtual ~RasterDBSourceOperator();

#ifndef MAPPING_OPERATOR_STUBS
		virtual void getProvenance(ProvenanceCollection &pc);
		virtual std::unique_ptr<GenericRaster> getRaster(const QueryRectangle &rect, const QueryTools &tools);
#endif
	protected:
		void writeSemanticParameters(std::ostringstream &stream);
	private:
#ifndef MAPPING_OPERATOR_STUBS
		std::shared_ptr<RasterDB> rasterdb;
#endif
		std::string sourcename;
		int channel;
		bool transform;
};


// RasterSource Operator
RasterDBSourceOperator::RasterDBSourceOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params) : GenericOperator(sourcecounts, sources) {
	assumeSources(0);
	sourcename = params.get("sourcename", "").asString();
	if (sourcename.length() == 0)
		throw OperatorException("SourceOperator: missing sourcename");

#ifndef MAPPING_OPERATOR_STUBS
	rasterdb = RasterDB::open(sourcename.c_str(), RasterDB::READ_ONLY);
#endif
	channel = params.get("channel", 0).asInt();
	transform = params.get("transform", true).asBool();
}

RasterDBSourceOperator::~RasterDBSourceOperator() {
}

REGISTER_OPERATOR(RasterDBSourceOperator, "rasterdb_source");


#ifndef MAPPING_OPERATOR_STUBS
void RasterDBSourceOperator::getProvenance(ProvenanceCollection &pc) {
	std::string local_identifier = "data." + getType() + "." + sourcename;

	auto sp = rasterdb->getProvenance();
	if (sp == nullptr)
		pc.add(Provenance("", "", "", local_identifier));
	else
		pc.add(Provenance(sp->citation, sp->license, sp->uri, local_identifier));
}

std::unique_ptr<GenericRaster> RasterDBSourceOperator::getRaster(const QueryRectangle &rect, const QueryTools &tools) {
	return rasterdb->query(rect, tools.profiler, channel, transform);
}
#endif

void RasterDBSourceOperator::writeSemanticParameters(std::ostringstream &stream) {
	std::string trans = transform ? "true" : "false";
	stream << "{\"sourcename\": \"" << sourcename << "\",";
	stream << " \"channel\": " << channel << ",";
	stream << " \"transform\": " << trans << "}";
}
