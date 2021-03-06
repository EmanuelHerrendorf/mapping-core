#ifndef DATATYPES_COLORIZER_H
#define DATATYPES_COLORIZER_H

#include <memory>
#include <vector>

#include <stdint.h>
#include <json/json.h>


using color_t = uint32_t;

color_t color_from_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

class Unit;

static color_t defaultNoDataColor = color_from_rgba(0, 0, 0, 0);
static color_t defaultDefaultColor = color_from_rgba(255, 0, 255, 0);

/*
 * This is a basic Colorizer, based on a table of value:color pairs.
 * The color of a pixel is determined by interpolating between the nearest Breakpoints.
 */
class Colorizer {
	public:
		struct Breakpoint {
			Breakpoint(double v, color_t c) : value(v), color(c) {}
			double value;
			color_t color;
		};
		enum class Interpolation {
			NEAREST,
			LINEAR
		};
		using ColorTable = std::vector<Breakpoint>;

		Colorizer(ColorTable table, Interpolation interpolation = Interpolation::LINEAR,
                  color_t nodataColor = defaultNoDataColor, color_t defaultColor = defaultDefaultColor);
		virtual ~Colorizer();

		void fillPalette(color_t *colors, int num_colors, double min, double max) const;
		std::string toJson() const;

		/**
		 * create colorizer from json specification
		 */
		static std::unique_ptr<Colorizer> fromJson(const Json::Value &json);

		/**
		 * create greyscale colorizer based on min/max values
		 */
        static std::unique_ptr<Colorizer> greyscale(double min, double max);

        /**
         * get the default colorizer for error output
         */
        static const Colorizer &error();

        double minValue() const {
            return table.front().value;
        }

        double maxValue() const {
            return table.back().value;
        }

        color_t getNoDataColor() const {
            return nodataColor;
        }

        color_t getDefaultColor() const {
            return defaultColor;
        }

private:
		ColorTable table;
		Interpolation interpolation;
		color_t nodataColor = defaultNoDataColor;
		color_t defaultColor = defaultDefaultColor;
};

#endif
