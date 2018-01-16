// This algorithm is based on Blanco-Muriel, Manuel, et al. "Computing the solar vector." Solar Energy 70.5 (2001): 431-441.

// 18.08.2014 -  Johannes Dr�nner: Modified this file to split intermediate variables from actual positions
#ifndef __SUNPOS_H
#define __SUNPOS_H


// Declaration of some constants 
#define pi    3.14159265358979323846
#define twopi (2*pi)
#define rad   (pi/180)
#define dEarthMeanRadius     6371.01	// In km
#define dAstronomicalUnit    149597890	// In km

struct cTime
{
	int iYear;
	int iMonth;
	int iDay;
	double dHours;
	double dMinutes;
	double dSeconds;
};

struct cLocation
{
	double dLongitude;
	double dLatitude;
};

struct cSunCoordinates
{
	double dZenithAngle;
	double dAzimuth;
};

//This struct holds the intermediate variables
struct cIntermediateVariables{
	double dGreenwichMeanSiderealTime;
	double dRightAscension;
	double dDeclination;
};

//void sunpos(cTime udtTime, cLocation udtLocation, cSunCoordinates *udtSunCoordinates);
cIntermediateVariables sunposIntermediate(int iYear, int iMonth, int iDay, double dHours, double dMinutes, double dSeconds);
#endif
