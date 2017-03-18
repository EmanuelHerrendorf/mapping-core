#ifndef PLOT_PNG_H
#define PLOT_PNG_H

#include <string>

#include "datatypes/plot.h"

/**
 * This plot outputs a png image as base64 encapsulated in JSON
 */
class PNGPlot : public GenericPlot {
	public:
		PNGPlot(const std::string &binary);
		virtual ~PNGPlot();

		const std::string toJSON() const;

		std::unique_ptr<GenericPlot> clone() const {
			return make_unique<PNGPlot>(binary);
		}

	private:
		std::string binary;
};

#endif
