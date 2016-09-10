#include "datatypes/raster.h"
#include "datatypes/raster/typejuggling.h"
#include "raster/profiler.h"
#include "raster/opencl.h"
#include "operators/operator.h"
#include "msg_constants.h"

#include <limits>
#include <memory>
#include <math.h>
#include <ctime>
#include <json/json.h>
#include <gdal_priv.h>

/*
 * This is an implementation of a pansharpening algorithm for meteosat data.
 * For an overview of pansharpening techniques, see:
 * http://en.wikipedia.org/wiki/Pansharpened_image
 *
 * The specific algorithm is published in:
 * "1 km fog and low stratus detection using pan-sharpened MSG SEVIRI data"
 * by H. M. Schulz, B. Thies, J. Cermak and J. Bendix
 * http://www.atmos-meas-tech.net/5/2469/2012/amt-5-2469-2012.pdf
 */

class MeteosatPansharpeningOperator : public GenericOperator {
	public:
		MeteosatPansharpeningOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params);
		virtual ~MeteosatPansharpeningOperator();

#ifndef MAPPING_OPERATOR_STUBS
		virtual std::unique_ptr<GenericRaster> getRaster(const QueryRectangle &rect, const QueryTools &tools);
#endif
	protected:
		void writeSemanticParameters(std::ostringstream& stream);
	private:
		int local_regression;
		bool spatial;
		int distance;
};


MeteosatPansharpeningOperator::MeteosatPansharpeningOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params) : GenericOperator(sourcecounts, sources) {
	assumeSources(1);

	local_regression = params.get("local_regression", 5).asInt();
	spatial = params.get("spatial", false).asBool();
	distance = params.get("distance", 1).asInt();
}
MeteosatPansharpeningOperator::~MeteosatPansharpeningOperator() {
}
REGISTER_OPERATOR(MeteosatPansharpeningOperator, "meteosat_pansharpening");

void MeteosatPansharpeningOperator::writeSemanticParameters(std::ostringstream& stream) {
	stream << "{";
	stream << "\"local_regression\": " << local_regression << ",";
	stream << "\"spatial\": " << (spatial ? "true" : "false") << ",";
	stream << "\"distance\": " << distance;
	stream << "}";
}


#ifndef MAPPING_OPERATOR_STUBS
#ifdef MAPPING_NO_OPENCL
std::unique_ptr<GenericRaster> MeteosatPansharpeningOperator::getRaster(const QueryRectangle &rect, const QueryTools &tools) {
	throw OperatorException("MSG_Pansharpening_Operator: cannot be executed without OpenCL support");
}
#else


#include "operators/processing/meteosat/pansharpening_degenerate.cl.h"
#include "operators/processing/meteosat/pansharpening_regression.cl.h"
#include "operators/processing/meteosat/pansharpening_interpolate.cl.h"

std::unique_ptr<GenericRaster> MeteosatPansharpeningOperator::getRaster(const QueryRectangle &rect, const QueryTools &tools) {
	RasterOpenCL::init();

	auto raster_lowres = getRasterFromSource(1, rect, tools, RasterQM::LOOSE);

	// query the HRV canal with triple the resolution
	QueryRectangle rect2(*raster_lowres);
	rect2.xres = raster_lowres->width * 3;
	rect2.yres = raster_lowres->height * 3;

	auto raster_hrv = getRasterFromSource(0, rect2, tools, RasterQM::EXACT);

	Profiler::Profiler p("CL_PANSHARPENING_OPERATOR");
	raster_hrv->setRepresentation(GenericRaster::OPENCL);

	if(raster_hrv->width % raster_lowres->width != 0 || raster_hrv->height % raster_lowres->height != 0)
		throw ArgumentException("PansharpeningOperator: ratio between HRV and lowres canal is invalid\n");

	int ratio = raster_hrv->width / raster_lowres->width;
	if (ratio != 3)
		throw ArgumentException("PansharpeningOperator: ratio between HRV and lowres canal is not 3\n");

	std::vector<float> spatialMatrix {
		0.000683f,0.001347f,0.002680f,0.003929f,0.004373f,0.003929f,0.002680f,0.001347f,0.000683f,//0
		0.000885f,0.003331f,0.007027f,0.010179f,0.011055f,0.010179f,0.007027f,0.003331f,0.000885f,//1
		0.002129f,0.007592f,0.015244f,0.022005f,0.024218f,0.022005f,0.015244f,0.007592f,0.002129f,//2
		0.003886f,0.012650f,0.024532f,0.035293f,0.039513f,0.035293f,0.024532f,0.012650f,0.003886f,//3
		0.004785f,0.015318f,0.029237f,0.041485f,0.046473f,0.041485f,0.029237f,0.015318f,0.004785f,//4
		0.003886f,0.012650f,0.024532f,0.035293f,0.039513f,0.035293f,0.024532f,0.012650f,0.003886f,//5
		0.002129f,0.007592f,0.015244f,0.022005f,0.024218f,0.022005f,0.015244f,0.007592f,0.002129f,//6
		0.000885f,0.003331f,0.007027f,0.010179f,0.011055f,0.010179f,0.007027f,0.003331f,0.000885f,//7
		0.000683f,0.001347f,0.002680f,0.003929f,0.004373f,0.003929f,0.002680f,0.001347f,0.000683f//8
	};

	//TODO:compute overall maximum:

	// Degenerate:
	// degenerate high res matrix to low res:
	auto low_high_matrix = GenericRaster::create(raster_lowres->dd, *raster_lowres, GenericRaster::OPENCL);

	RasterOpenCL::CLProgram prog_degenerate;
	prog_degenerate.setProfiler(tools.profiler);
	prog_degenerate.addInRaster(raster_hrv.get());
	prog_degenerate.addOutRaster(low_high_matrix.get());
	if (!spatial) {
		prog_degenerate.compile(operators_processing_meteosat_pansharpening_degenerate, "pan_downsample");
		prog_degenerate.addArg(ratio);
	} else {
		prog_degenerate.compile(operators_processing_meteosat_pansharpening_degenerate, "pan_downsample_spatial");
		prog_degenerate.addArg(ratio);
		prog_degenerate.addArg(9);
		prog_degenerate.addArg(spatialMatrix);
	}
	prog_degenerate.run();

	// Regression:
	// estimate regression of low res matrix with degenerated high res matrix:
	auto reg_low_a = GenericRaster::create(raster_lowres->dd, *raster_lowres, GenericRaster::OPENCL);
	auto reg_low_b = GenericRaster::create(raster_lowres->dd, *raster_lowres, GenericRaster::OPENCL);

	RasterOpenCL::CLProgram prog_regression;
	prog_regression.setProfiler(tools.profiler);
	prog_regression.addInRaster(raster_lowres.get());
	prog_regression.addInRaster(low_high_matrix.get());
	prog_regression.addOutRaster(reg_low_a.get());
	prog_regression.addOutRaster(reg_low_b.get());
	prog_regression.compile(operators_processing_meteosat_pansharpening_regression, "pan_regression");
	prog_regression.addArg(local_regression);
	prog_regression.addArg(distance);
	prog_regression.run();

	low_high_matrix.reset(nullptr);

	// Interpolate:
	// interpolate low res matrices to high res and combine them to the result matrix:

	TemporalReference tref(raster_hrv->stref);
	tref.intersect(raster_lowres->stref);
	SpatioTemporalReference stref(raster_hrv->stref, tref);
	auto raster_out = GenericRaster::create(raster_lowres->dd, stref, raster_hrv->width, raster_hrv->height, 0, GenericRaster::OPENCL);

	RasterOpenCL::CLProgram prog_interpolate;
	prog_interpolate.setProfiler(tools.profiler);
	prog_interpolate.addInRaster(reg_low_a.get());
	prog_interpolate.addInRaster(reg_low_b.get());
	prog_interpolate.addInRaster(raster_hrv.get());
	prog_interpolate.addInRaster(raster_lowres.get());
	prog_interpolate.addOutRaster(raster_out.get());
	prog_interpolate.compile(operators_processing_meteosat_pansharpening_interpolate, "pan_interpolate");
	prog_interpolate.addArg(ratio);
	prog_interpolate.run();

	return raster_out;
}
#endif
#endif
