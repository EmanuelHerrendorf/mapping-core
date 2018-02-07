
#include "simplefeaturecollection.h"
#include "util/exceptions.h"
#include "util/binarystream.h"
#include "util/make_unique.h"
#include "util/sizeutil.h"

#include <sstream>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <limits>
#include <json/json.h>


/**
 * Timestamps
 */
bool SimpleFeatureCollection::hasTime() const {
	return time.size() == getFeatureCount();
}

void SimpleFeatureCollection::setTimeStamps(std::vector<double> &&time_start, std::vector<double> &&time_end) {
	if(time_start.size() != getFeatureCount())
		throw ArgumentException("setTimeStamps: size of time_start invalid");
	if(time_end.size() != getFeatureCount())
		throw ArgumentException("setTimeStamps: size of time_end invalid");

	time.resize(getFeatureCount());
	for(size_t i = 0; i < time_start.size(); ++i) {
		time[i] = TimeInterval(time_start[i], time_end[i]);
	}
}

void SimpleFeatureCollection::addDefaultTimestamps() {
	addDefaultTimestamps(stref.TemporalReference::beginning_of_time(), stref.TemporalReference::end_of_time());
}

void SimpleFeatureCollection::addDefaultTimestamps(double min, double max) {
	if (hasTime())
		return;
	auto fcount = getFeatureCount();
	time.empty();
	time.resize(fcount, TimeInterval(min, max));
}

/*
 * Validation
 */
void SimpleFeatureCollection::validate() const {
	auto fcount = getFeatureCount();
	if (time.size() > 0 || time.size() > 0) {
		if (time.size() != fcount || time.size() != fcount)
			throw ArgumentException("SimpleFeatureCollection: size of the time-arrays doesn't match feature count");

		auto bot = stref.beginning_of_time();
		auto eot = stref.end_of_time();
		for(auto& interval : time)
			interval.validate(bot, eot);
	}

	feature_attributes.validate(fcount);

	validateSpecifics();
}


/*
 * Export
 */
std::string SimpleFeatureCollection::toGeoJSON(bool displayMetadata) const {
	std::ostringstream json;
	json << std::fixed; // std::setprecision(4);

	json << "{\"type\":\"FeatureCollection\",\"crs\":{\"type\":\"name\",\"properties\":{\"name\":\"" << stref.crsId.to_string() <<"\"}},\"features\":[";

	auto value_keys = feature_attributes.getNumericKeys();
	auto string_keys = feature_attributes.getTextualKeys();
	bool isSimpleCollection = isSimple();
	for (size_t feature = 0; feature < getFeatureCount(); ++feature) {
		json << "{\"type\":\"Feature\",\"geometry\":";
		featureToGeoJSONGeometry(feature, json);

		if(displayMetadata && (string_keys.size() > 0 || value_keys.size() > 0 || hasTime())){
			json << ",\"properties\":{";

			//TODO: handle missing metadata values
			for (auto &key : string_keys) {
				json << "\"" << key << "\":" << Json::valueToQuotedString(feature_attributes.textual(key).get(feature).c_str()) << ",";
			}

			for (auto &key : value_keys) {
				double value = feature_attributes.numeric(key).get(feature);
				json << "\"" << key << "\":";
				if (std::isfinite(value)) {
					json << value;
				}
				else {
					json << "null";
				}

				json << ",";
			}

			if (hasTime()) {
				json << "\"time_start\":\"" << stref.toIsoString(time[feature].t1) << "\",\"time_end\":\"" << stref.toIsoString(time[feature].t2) << "\",";
			}

			json.seekp(((long) json.tellp()) - 1); // delete last ,
			json << "}";
		}
		json << "},";

	}

	if(getFeatureCount() > 0)
		json.seekp(((long) json.tellp()) - 1); // delete last ,
	json << "]}";

	return json.str();
}


std::string SimpleFeatureCollection::toWKT() const {
	std::ostringstream wkt;

	wkt << "GEOMETRYCOLLECTION(";

	for(size_t i = 0; i < getFeatureCount(); ++i){
		featureToWKT(i, wkt);
		wkt << ",";
	}
	if(getFeatureCount() > 0)
		wkt.seekp(((long) wkt.tellp()) - 1); // delete last ,

	wkt << ")";

	return wkt.str();
}

std::string SimpleFeatureCollection::toCSV() const {
	//TODO: include global metadata
	std::ostringstream csv;
	csv << std::fixed; // std::setprecision(4);

	auto string_keys = feature_attributes.getTextualKeys();
	auto value_keys = feature_attributes.getNumericKeys();

	bool isSimpleCollection = isSimple();

	//header
	csv << "wkt";
	if (hasTime())
		csv << ",\"time_start\",\"time_end\"";
	for(auto &key : string_keys) {
		csv << ",\"" << key << "\"";
	}
	for(auto &key : value_keys) {
		csv << ",\"" << key << "\"";
	}
	csv << std::endl;

	for (size_t featureIndex = 0; featureIndex < getFeatureCount(); ++featureIndex) {
		csv << "\"";
		featureToWKT(featureIndex, csv);
		csv << "\"";
		if (hasTime()){
			csv << "," << "\"" << stref.toIsoString(time[featureIndex].t1) << "\"" << ","
					 << "\"" << stref.toIsoString(time[featureIndex].t2) << "\"";
		}

		//TODO: handle missing metadata values
		for(auto &key : string_keys) {
			csv << ",\"" << feature_attributes.textual(key).get(featureIndex) << "\"";
		}
		for(auto &key : value_keys) {
			csv << "," << feature_attributes.numeric(key).get(featureIndex);
		}
		csv << std::endl;
	}

	return csv.str();
}

std::string SimpleFeatureCollection::featureToWKT(size_t featureIndex) const{
	std::ostringstream wkt;
	featureToWKT(featureIndex, wkt);
	return wkt.str();
}

std::string SimpleFeatureCollection::toARFF(std::string layerName) const {
	std::ostringstream arff;

	arff << "@RELATION " << layerName << std::endl << std::endl;

	arff << "@ATTRIBUTE wkt STRING" << std::endl;

	if (hasTime()){
		arff << "@ATTRIBUTE time_start DATE" << std::endl;
		arff << "@ATTRIBUTE time_end DATE" << std::endl;
	}

	auto string_keys = feature_attributes.getTextualKeys();
	auto value_keys = feature_attributes.getNumericKeys();

	for(auto &key : string_keys) {
		arff << "@ATTRIBUTE" << " " << key << " " << "STRING" << std::endl;
	}
	for(auto &key : value_keys) {
		arff << "@ATTRIBUTE" << " " << key << " " << "NUMERIC" << std::endl;
	}

	arff << std::endl;
	arff << "@DATA" << std::endl;

	for (size_t featureIndex = 0; featureIndex < getFeatureCount(); ++featureIndex) {
		arff << "\"";
		featureToWKT(featureIndex, arff);
		arff << "\"";
		if (hasTime()){
			arff << "," << "\"" << stref.toIsoString(time[featureIndex].t1) << "\"" << ","
					 << "\"" << stref.toIsoString(time[featureIndex].t2) << "\"";
		}

		//TODO: handle missing metadata values
		for(auto &key : string_keys) {
			arff << ",\"" << feature_attributes.textual(key).get(featureIndex) << "\"";
		}
		for(auto &key : value_keys) {
			arff << "," << feature_attributes.numeric(key).get(featureIndex);
		}
		arff << std::endl;
	}

	return arff.str();
}

SpatialReference SimpleFeatureCollection::calculateMBR(size_t coordinateIndexStart, size_t coordinateIndexStop) const {
	if(coordinateIndexStart >= coordinates.size() || coordinateIndexStop > coordinates.size() || coordinateIndexStart >= coordinateIndexStop)
		throw ArgumentException("Invalid start/stop index for coordinates");

	SpatialReference reference(stref.crsId);

	const Coordinate& c0 = coordinates[coordinateIndexStart];
	reference.x1 = c0.x;
	reference.x2 = c0.x;
	reference.y1 = c0.y;
	reference.y2 = c0.y;

	for(size_t i = coordinateIndexStart + 1; i < coordinateIndexStop; ++i){
		const Coordinate& c = coordinates[i];

		if(c.x < reference.x1)
			reference.x1 = c.x;
		else if(c.x > reference.x2)
			reference.x2 = c.x;

		if(c.y < reference.y1)
			reference.y1 = c.y;
		else if(c.y > reference.y2)
			reference.y2 = c.y;
	}

	return reference;
}

SpatialReference SimpleFeatureCollection::getCollectionMBR() const {
	return calculateMBR(0, coordinates.size());
}

/**
 * check if Coordinate c is on line given by Coordinate p1, p2
 * @param p1 start of line
 * @param p2 end of line
 * @param c coordinate to check, must be collinear to p1, p2
 * @return true if c is on line segment
 */
bool onSegment(const Coordinate& p1, const Coordinate& p2, const Coordinate& c)
{
    if (c.x <= std::max(p1.x, p2.x) && c.x >= std::min(p1.x, p2.x) &&
        c.y <= std::max(p1.y, p2.y) && c.y >= std::min(p1.y, p2.y))
       return true;

    return false;
}
enum Orientation {
	LEFT, RIGHT, ON
};

/**
 * calculate orientation of coordinate c with respect to line from p1 to p2
 * @param p1 start of line
 * @param p2 end of line
 * @param c coordinate to check
 * @return the orientation of c
 */
Orientation orientation(const Coordinate& p1, const Coordinate& p2, const Coordinate& c) {
    double val = (p2.y - p1.y) * (c.x - p2.x) -
              (p2.x - p1.x) * (c.y - p2.y);

    if (val == 0) return ON;  // colinear

    return (val > 0)? RIGHT: LEFT;
}

size_t SimpleFeatureCollection::get_byte_size() const {
	return SpatioTemporalResult::get_byte_size() +
		   SizeUtil::get_byte_size(coordinates) +
		   SizeUtil::get_byte_size(time) +
		   feature_attributes.get_byte_size();

}

bool SimpleFeatureCollection::lineSegmentsIntersect(const Coordinate& p1, const Coordinate& p2, const Coordinate& p3, const Coordinate& p4) const {
	// idea from: http://www.geeksforgeeks.org/check-if-two-given-line-segments-intersect/

	//TODO: quick check if bounding box intersects to exit early?

	Orientation o1 = orientation(p1, p2, p3);
	Orientation o2 = orientation(p1, p2, p4);
	Orientation o3 = orientation(p3, p4, p1);
	Orientation o4 = orientation(p3, p4, p2);

	// General case
	if (o1 != o2 && o3 != o4)
		return true;

	// Special Cases
	// p1, p2 and p3 are collinear and p3 lies on segment p1p2
	if (o1 == ON && onSegment(p1, p2, p3)) return true;

	// p1, p2 and p3 are collinear and p4 lies on segment p1p2
	if (o2 == ON && onSegment(p1, p2, p4)) return true;

	// p3, p4 and p1 are colinear and p1 lies on segment p3p4
	if (o3 == ON && onSegment(p3, p4, p1)) return true;

	 // p3, p4 and p2 are collinear and p2 lies on segment p3p4
	if (o4 == ON && onSegment(p3, p4, p2)) return true;

	return false;
}

bool SimpleFeatureCollection::featureIntersectsRectangle(size_t featureIndex, const SpatialReference& sref) const{
	return featureIntersectsRectangle(featureIndex, sref.x1, sref.y1, sref.x2, sref.y2);
}


std::vector<bool> SimpleFeatureCollection::getKeepVectorForFilterBySpatioTemporalReferenceIntersection(const SpatioTemporalReference& stref) const {
	if (stref.crsId != this->stref.crsId)
		throw ArgumentException("Cannot filter a SimpleFeatureCollection with a SpatialReference in a different crsId.");
	if (stref.timetype != this->stref.timetype)
		throw ArgumentException("Cannot filter a SimpleFeatureCollection with a SpatialReference in a different timetype.");

	auto size = this->getFeatureCount();
	std::vector<bool> keep(size);

	if (!hasTime()) {
		for (size_t feature=0;feature<size;feature++)
			keep[feature] = this->featureIntersectsRectangle(feature, stref.x1, stref.y1, stref.x2, stref.y2);
	}
	else {
		for (size_t feature=0;feature<size;feature++)
			keep[feature] = this->featureIntersectsRectangle(feature, stref.x1, stref.y1, stref.x2, stref.y2)
				&& stref.intersects(this->time[feature].t1, this->time[feature].t2);
	}
	return keep;
}

size_t SimpleFeatureCollection::calculate_kept_count(const std::vector<bool> &keep) const {
	size_t count = getFeatureCount();
	if (keep.size() != count) {
		throw ArgumentException(concat("SimpleFeatureCollection::filter(): size of filter does not match (", keep.size(), " != ",count, ")"));
	}

	size_t kept_count = 0;
	for (size_t idx=0;idx<keep.size();idx++) {
		if (keep[idx])
			kept_count++;
	}
	return kept_count;
}

size_t SimpleFeatureCollection::calculate_kept_count(const std::vector<char> &keep) const {
	size_t count = getFeatureCount();
	if (keep.size() != count) {
		throw ArgumentException(concat("SimpleFeatureCollection::filter(): size of filter does not match (", keep.size(), " != ",count, ")"));
	}

	size_t kept_count = 0;
	for (size_t idx=0;idx<keep.size();idx++) {
		if (keep[idx])
			kept_count++;
	}
	return kept_count;
}

void SimpleFeatureCollection::addGlobalAttributesFromCollection(const SimpleFeatureCollection &collection) {
	for(auto& attribute : collection.global_attributes.textual()){
		global_attributes.setTextual(attribute.first, attribute.second);
	}
	for(auto& attribute : collection.global_attributes.numeric()){
		global_attributes.setNumeric(attribute.first, attribute.second);
	}
}

void SimpleFeatureCollection::addFeatureAttributesFromCollection(const SimpleFeatureCollection &collection) {
	for(auto& attribute : collection.feature_attributes.getTextualKeys()){
		feature_attributes.addTextualAttribute(attribute, collection.feature_attributes.textual(attribute).unit);
	}
	for(auto& attribute : collection.feature_attributes.getNumericKeys()){
		feature_attributes.addNumericAttribute(attribute, collection.feature_attributes.numeric(attribute).unit);
	}
}

void SimpleFeatureCollection::setAttributesAndTimeFromCollection(const SimpleFeatureCollection &collection, size_t collectionIndex, size_t thisIndex, const std::vector<std::string> &textualAttributes, const std::vector<std::string> &numericAttributes) {
	//time
	if(collection.hasTime()) {
		if(!hasTime())
			addDefaultTimestamps();
		time[thisIndex] = collection.time[collectionIndex];
	}

	//feature attributes
	for(auto& attribute : textualAttributes){
		feature_attributes.textual(attribute).set(thisIndex, collection.feature_attributes.textual(attribute).get(collectionIndex));
	}
	for(auto& attribute : numericAttributes){
		feature_attributes.numeric(attribute).set(thisIndex, collection.feature_attributes.numeric(attribute).get(collectionIndex));
	}
}
