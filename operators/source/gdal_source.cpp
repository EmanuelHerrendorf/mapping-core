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
 * Operator that gets raster data from file via gdal
 *
 */
class RasterGDALSourceOperator : public GenericOperator {
	public:
		RasterGDALSourceOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params);
		virtual ~RasterGDALSourceOperator();

		virtual void getProvenance(ProvenanceCollection &pc);
		virtual std::unique_ptr<GenericRaster> getRaster(const QueryRectangle &rect, const QueryTools &tools);
	protected:
		void writeSemanticParameters(std::ostringstream &stream);
	private:
		std::string sourcename;
		int channel;
		bool transform;
		std::unique_ptr<GenericRaster> loadDataset( const char *filename, 
													int rasterid, 
													bool &flipx, bool &flipy, 
													epsg_t epsg, 
													bool clip, 
													double x1, double y1, 
													double x2, double y2);
		
		std::unique_ptr<GenericRaster> loadRaster(GDALDataset *dataset, int rasteridx, double origin_x, double origin_y, 
																					   double scale_x, double scale_y, 
																					   bool &flipx, bool &flipy, 
																					   epsg_t default_epsg, bool clip, 
																					   double clip_x1, double clip_y1, 
																					   double clip_x2, double clip_y2);

};


RasterGDALSourceOperator::RasterGDALSourceOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params) : GenericOperator(sourcecounts, sources) {
	//assumeSources(0);
	sourcename = params.get("sourcename", "").asString();
	if (sourcename.length() == 0)
		throw OperatorException("SourceOperator: missing sourcename");

	
	channel = params.get("channel", 1).asInt();
	transform = params.get("transform", true).asBool();
	
}

RasterGDALSourceOperator::~RasterGDALSourceOperator(){

}

REGISTER_OPERATOR(RasterGDALSourceOperator, "gdal_source");

void RasterGDALSourceOperator::getProvenance(ProvenanceCollection &pc) {
	/*std::string local_identifier = "data." + getType() + "." + sourcename;

	auto sp = rasterdb->getProvenance();
	if (sp == nullptr)
		pc.add(Provenance("", "", "", local_identifier));
	else
		pc.add(Provenance(sp->citation, sp->license, sp->uri, local_identifier));
		*/
}


std::unique_ptr<GenericRaster> RasterGDALSourceOperator::getRaster(const QueryRectangle &rect, const QueryTools &tools) {
	// soll der operator komplett hier implementiert werden? 

	bool flipX = false, flipY = false;
	
	return loadDataset(sourcename.c_str(), channel, flipX, flipY, rect->epsg, )
}

void RasterGDALSourceOperator::writeSemanticParameters(std::ostringstream &stream) {
	std::string trans = transform ? "true" : "false";
	stream << "{\"sourcename\": \"" << sourcename << "\",";
	stream << " \"channel\": " << channel << ",";
	stream << " \"transform\": " << trans << "}";
}



std::unique_ptr<GenericRaster> RasterGDALSourceOperator::loadRaster(GDALDataset *dataset, int rasteridx, double origin_x, double origin_y, double scale_x, double scale_y, bool &flipx, bool &flipy, epsg_t default_epsg, bool clip, double clip_x1, double clip_y1, double clip_x2, double clip_y2) {
	GDALRasterBand  *poBand;
	int             nBlockXSize, nBlockYSize;
	int             bGotMin, bGotMax;
	double          adfMinMax[2];

	poBand = dataset->GetRasterBand( rasteridx );
	poBand->GetBlockSize( &nBlockXSize, &nBlockYSize );

	GDALDataType type = poBand->GetRasterDataType();

/*
	printf( "Block=%dx%d Type=%s, ColorInterp=%s\n",
		nBlockXSize, nBlockYSize,
		GDALGetDataTypeName(poBand->GetRasterDataType()),
		GDALGetColorInterpretationName(
				poBand->GetColorInterpretation()) );
*/
	adfMinMax[0] = poBand->GetMinimum( &bGotMin );
	adfMinMax[1] = poBand->GetMaximum( &bGotMax );
	if( ! (bGotMin && bGotMax) )
		GDALComputeRasterMinMax((GDALRasterBandH)poBand, TRUE, adfMinMax);

	int hasnodata = true;
	int success;
	double nodata = poBand->GetNoDataValue(&success);
	if (!success /*|| nodata < adfMinMax[0] || nodata > adfMinMax[1]*/) {
		hasnodata = false;
		nodata = 0;
	}
	/*
	if (nodata < adfMinMax[0] || nodata > adfMinMax[1]) {

	}
	*/
/*
	printf( "Min=%.3g, Max=%.3g\n", adfMinMax[0], adfMinMax[1] );

	if( poBand->GetOverviewCount() > 0 )
		printf( "Band has %d overviews.\n", poBand->GetOverviewCount() );

	if( poBand->GetColorTable() != NULL )
		printf( "Band has a color table with %d entries.\n",
			poBand->GetColorTable()->GetColorEntryCount() );
*/

	// Figure out the data type
	epsg_t epsg = default_epsg;
	double minvalue = adfMinMax[0];
	double maxvalue = adfMinMax[1];

	//if (type == GDT_Byte) maxvalue = 255;
	if(epsg == EPSG_GEOSMSG){
		hasnodata = true;
		nodata = 0;
		type = GDT_Int16; // TODO: sollte GDT_UInt16 sein!
	}

	// Figure out which pixels to load
	int   nXSize = poBand->GetXSize();
	int   nYSize = poBand->GetYSize();

	int pixel_x1 = 0;
	int pixel_y1 = 0;
	int pixel_width = nXSize;
	int pixel_height = nYSize;
	if (clip) {
		pixel_x1 = floor((clip_x1 - origin_x) / scale_x);
		pixel_y1 = floor((clip_y1 - origin_y) / scale_y);
		int pixel_x2 = floor((clip_x2 - origin_x) / scale_x);
		int pixel_y2 = floor((clip_y2 - origin_y) / scale_y);

		if (pixel_x1 > pixel_x2)
			std::swap(pixel_x1, pixel_x2);
		if (pixel_y1 > pixel_y2)
			std::swap(pixel_y1, pixel_y2);

		pixel_x1 = std::max(0, pixel_x1);
		pixel_y1 = std::max(0, pixel_y1);

		pixel_x2 = std::min(nXSize-1, pixel_x2);
		pixel_y2 = std::min(nYSize-1, pixel_y2);

		pixel_width = pixel_x2 - pixel_x1 + 1;
		pixel_height = pixel_y2 - pixel_y1 + 1;
	}

	double x1 = origin_x + scale_x * (pixel_x1 - 0.5);
	double y1 = origin_y + scale_y * (pixel_y1 - 0.5);
	double x2 = x1 + scale_x * pixel_width;
	double y2 = y1 + scale_y * pixel_height;

	SpatioTemporalReference stref(
		SpatialReference(epsg, x1, y1, x2, y2, flipx, flipy),
		TemporalReference::unreferenced()
	);
	Unit unit = Unit::unknown();
	unit.setMinMax(minvalue, maxvalue);
	DataDescription dd(type, unit, hasnodata, nodata);
	//printf("loading raster with %g -> %g valuerange\n", adfMinMax[0], adfMinMax[1]);

	auto raster = GenericRaster::create(dd, stref, pixel_width, pixel_height);
	void *buffer = raster->getDataForWriting();
	//int bpp = raster->getBPP();

	/*
CPLErr GDALRasterBand::RasterIO( GDALRWFlag eRWFlag,
                                 int nXOff, int nYOff, int nXSize, int nYSize,
                                 void * pData, int nBufXSize, int nBufYSize,
                                 GDALDataType eBufType,
                                 int nPixelSpace,
                                 int nLineSpace )
	*/

	// Read Pixel data
	auto res = poBand->RasterIO( GF_Read,
		pixel_x1, pixel_y1, pixel_width, pixel_height, // rectangle in the source raster
		buffer, raster->width, raster->height, // position and size of the destination buffer
		type, 0, 0);

	if (res != CE_None)
		throw ImporterException("GDAL: RasterIO failed");

	// Selectively read metadata
	//char **mdList = GDALGetMetadata(poBand, "msg");
	if (epsg == EPSG_GEOSMSG) {
		char **mdList = poBand->GetMetadata("msg");
		for (int i = 0; mdList && mdList[i] != nullptr; i++ ) {
			//printf("GDALImport: got Metadata %s\n", mdList[i]);
			std::string md(mdList[i]);
			size_t split = md.find('=');
			if (split == std::string::npos)
				continue;

			std::string key = md.substr(0, split);
			std::string mkey = "msg." + key;
			std::string value = md.substr(split+1, std::string::npos);

			double dvalue = std::strtod(value.c_str(), nullptr);
			if (key == "TimeStamp" || (dvalue == 0 && value != "0")) {
				raster->global_attributes.setTextual(mkey, value);
			}
			else {
				raster->global_attributes.setNumeric(mkey, dvalue);
			}
		}
	}

	return raster;
}

std::unique_ptr<GenericRaster> RasterGDALSourceOperator::loadDataset(const char *filename, int rasterid, bool &flipx, bool &flipy, epsg_t epsg, bool clip, double x1, double y1, double x2, double y2) {
	GDAL::init();

	GDALDataset *dataset = (GDALDataset *) GDALOpen(filename, GA_ReadOnly);

	if (dataset == NULL)
		throw ImporterException(concat("Could not open dataset ", filename));


/*
	printf( "Driver: %s/%s\n",
		dataset->GetDriver()->GetDescription(),
		dataset->GetDriver()->GetMetadataItem( GDAL_DMD_LONGNAME ) );

	printf( "Size is %dx%dx%d\n",
		dataset->GetRasterXSize(), dataset->GetRasterYSize(),
		dataset->GetRasterCount() );

	if( dataset->GetProjectionRef()  != NULL )
			printf( "Projection is `%s'\n", dataset->GetProjectionRef() );
*/
	double adfGeoTransform[6];

	// http://www.gdal.org/classGDALDataset.html#af9593cc241e7d140f5f3c4798a43a668
	if( dataset->GetGeoTransform( adfGeoTransform ) != CE_None ) {
		GDALClose(dataset);
		throw ImporterException("no GeoTransform information in raster");
	}
/*
	if (adfGeoTransform[2] != 0 || adfGeoTransform[4] != 0) {
		GDALClose(dataset);
		std::stringstream ss;
		ss << "Raster is using an unsupported GeoTransform: (" << adfGeoTransform[0] << "/" << adfGeoTransform[1] << "/" << adfGeoTransform[2] << "/" << adfGeoTransform[3] << ")";
		throw ImporterException(ss.str());
	}
*/
/*
	{
		printf( "Origin = (%.6f,%.6f)\n",
			adfGeoTransform[0], adfGeoTransform[3] );

		printf( "Pixel Size = (%.6f,%.6f)\n",
			adfGeoTransform[1], adfGeoTransform[5] );
	}
*/

	int rastercount = dataset->GetRasterCount();

	if (rasterid < 1 || rasterid > rastercount) {
		GDALClose(dataset);
		throw ImporterException("rasterid not found");
	}


	const char *drivername = dataset->GetDriver()->GetDescription();
	//const char *drivername = dataset->GetDriverName();
	//printf("Driver: %s\n", drivername);
	if (strcmp(drivername, "MSG") == 0) {
		if (epsg != EPSG_GEOSMSG)
			throw ImporterException("MSG driver can only import rasters in MSG projection");
	}

	auto raster = GDALImporter_loadRaster(dataset, rasterid, adfGeoTransform[0], adfGeoTransform[3], adfGeoTransform[1], adfGeoTransform[5], flipx, flipy, epsg, clip, x1, y1, x2, y2);

	GDALClose(dataset);

	return raster;
}

