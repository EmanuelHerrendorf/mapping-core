#include <algorithm>
#include <pqxx/pqxx>
#include <json/json.h>
#include <cassert>
#include <numeric>
#include <future>
#include <queue>

#include "operators/source/natur40.h"
#include "util/configuration.h"

Natur40SourceOperator::Natur40SourceOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params)
        : GenericOperator(sourcecounts, sources) {
    assumeSources(0);

    auto connection_string = Configuration::get("operators.pgpointsource.dbcredentials");
    this->connection = make_unique<pqxx::connection>(connection_string);

    auto sensor_types = params.get("sensorTypes", Json::Value(Json::arrayValue));
    this->sensor_types.reserve(sensor_types.size() + 1);
    for (const auto &sensor_type_field : sensor_types) {
        const auto sensor_type = sensor_type_field.asString();
        const bool only_allowed_types = std::all_of(sensor_type.begin(), sensor_type.end(), [](char c) {
            return std::isalpha(c) || c == '_';
        });
        if (!only_allowed_types) {
            throw OperatorException {
                    "natur40_source: sensor types are only allowed to have alphanumerical values or underscore"
            };
        }
        this->sensor_types.push_back(sensor_type);
    }

    // add locations if missing
    if (std::none_of(this->sensor_types.begin(), this->sensor_types.end(),
                     [&](const std::string &t) { return t == "location"; })) {
        this->sensor_types.emplace_back("location");
    }
}

Natur40SourceOperator::~Natur40SourceOperator() = default;

REGISTER_OPERATOR(Natur40SourceOperator, "natur40_source");

auto Natur40SourceOperator::writeSemanticParameters(std::ostringstream &stream) -> void {
    Json::Value semantic_parameters{Json::objectValue};
    semantic_parameters["sensorTypes"] = Json::Value{Json::arrayValue};
    for (const auto &sensor_type : this->sensor_types) {
        semantic_parameters["sensorTypes"].append(sensor_type);
    }

    Json::FastWriter writer;
    stream << writer.write(semantic_parameters);
}

const std::string LAT_COLUMN_NAME{"latitude"}; /* NOLINT */
const std::string LON_COLUMN_NAME{"longitude"}; /* NOLINT */
const std::string TIME_START_COLUMN_NAME{"time_start"}; /* NOLINT */
//const std::string TIME_END_COLUMN_NAME{"time_end"}; /* NOLINT */
const std::string NODE_COLUMN_NAME{"node"}; /* NOLINT */

using table_t = std::string;
using column_t = std::string;

/**
 * Table row with extracted information
 */
class Row {
    public:
        Row(const pqxx::tuple &tuple, const std::vector<column_t> &columns) :
                node(tuple[NODE_COLUMN_NAME].as<std::string>()),
                time(tuple[TIME_START_COLUMN_NAME].as<double>()) {
            for (const auto &column_name : columns) {
                this->measurements[column_name] = tuple[column_name].is_null()
                                                  ? std::numeric_limits<double>::quiet_NaN()
                                                  : tuple[column_name].as<double>();
            }
        }

        std::string node;
        double time;
        std::map<std::string, double> measurements;
};

/**
 * Wrapper Class for an `Row` iterator and its end.
 */
class IterWrap {
        using container_t = std::vector<Row>;
        using iter_t = container_t::const_iterator;

    public:
        explicit IterWrap(container_t &container) :
                current(std::begin(container)),
                end(std::end(container)) {}

        auto node() const -> const std::string & {
            return this->current->node;
        }

        auto time() const -> double {
            return this->current->time;
        }

        auto row() const -> Row {
            return *this->current;
        }

        auto isEnd() const -> bool {
            return this->current == this->end;
        }

        auto operator++() -> void {
            ++this->current;
        }

    private:
        iter_t current;
        iter_t end;

};

/**
 * Reverse Comparator for using a Max Heap as a Min Heap
 */
class IterWrapComparator {
    public:
        auto operator()(const IterWrap &a, const IterWrap &b) -> int {
            if (a.isEnd()) {
                return true; // use b
            } else if (b.isEnd()) {
                return false; // use a
            }

            if (a.node() < b.node()) {
                return false; // use a
            } else if (a.node() > b.node()) {
                return true; // use b
            }

            return a.time() >= b.time();
        }
};

/**
 * Method that merges results of multiple containers and calls the function `f` in ordered manner.
 * @param results
 * @param f
 */
auto merge_results(std::map<table_t, std::vector<Row>> results, const std::function<void(const Row &)> &f) -> void {
    using iter = std::vector<Row>::const_iterator;

    std::priority_queue<IterWrap, std::vector<IterWrap>, IterWrapComparator> heap;
    for (auto &result : results) {
        heap.emplace(result.second);
    }

    while (!heap.empty()) {
        auto result = heap.top();
        heap.pop();

        if (result.isEnd()) {
            continue;
        }

        f(result.row());

        ++result;
        heap.push(result);
    }

}

/**
 * Add data to the point collection.
 * Filteres out invalid geo and time information
 * @param collection
 * @param node
 * @param time_start
 * @param time_end
 * @param values
 * @param qrect
 */
auto add_to_collection(PointCollection &collection, std::string &node,
                       double time_start, double time_end,
                       std::map<column_t, double> &values,
                       const QueryRectangle &qrect) -> void {
    double longitude = values[LON_COLUMN_NAME];
    double latitude = values[LAT_COLUMN_NAME];

    if (std::isnan(longitude) || std::isnan(latitude)) {
        return; // geo information is missing
    }
    if (time_start > qrect.t2 || time_end < qrect.t1) {
        return; // time is out of bounds
    }

    auto feature_id = collection.addSinglePointFeature(Coordinate(longitude, latitude));

    collection.time.emplace_back(time_start, time_end);

    collection.feature_attributes.textual(NODE_COLUMN_NAME).set(feature_id, node);

    for (const auto &value : values) {
        if (value.first != LON_COLUMN_NAME && value.first != LAT_COLUMN_NAME) {
            collection.feature_attributes.numeric(value.first).set(feature_id, value.second);
        }
    }
}

auto Natur40SourceOperator::getPointCollection(const QueryRectangle &rect,
                                               const QueryTools &tools) -> std::unique_ptr<PointCollection> {

    if (rect.crsId != CrsId::wgs84()) {
        throw OperatorException {"natur40_source: Must not load points in a projection other than wgs 84"};
    }

    pqxx::read_transaction transaction{*this->connection, "natur40_query"};

    // map sensor types to table names
    std::vector<table_t> tables;
    for (const auto &sensor_type : this->sensor_types) {
        tables.emplace_back(this->get_table_name(sensor_type));
    }

    std::map<table_t, std::vector<Row>> results;
    std::vector<column_t> measurement_columns;

    // create query
    for (const auto &table : tables) {
        const auto columns = get_columns_for_table(table);

        std::string query = table_query(table, columns, rect.t1, rect.t2);
        results[table] = std::vector<Row>{};
        pqxx::result result = transaction.exec(query);
        for (const auto &row : result) {
            results[table].emplace_back(row, columns);
        }

        for (const auto &column : columns) {
            measurement_columns.emplace_back(column);
        }
    }

    // initialize feature collection
    auto point_collection = make_unique<PointCollection>(rect);
    point_collection->feature_attributes.addTextualAttribute(NODE_COLUMN_NAME, Unit::unknown());
    for (const auto &column : measurement_columns) {
        if (column == LON_COLUMN_NAME || column == LAT_COLUMN_NAME) {
            continue; // do not save lon/lat in numeric attributes
        }
        point_collection->feature_attributes.addNumericAttribute(column, Unit::unknown());
    }

    std::map<column_t, double> current_values;
    for (const auto &column : measurement_columns) {
        current_values[column] = std::numeric_limits<double>::quiet_NaN();
    }
    std::string current_node;
    double current_time = rect.beginning_of_time();

    merge_results(results, [&](const Row &row) {
        if (row.node != current_node) {
            // finish node
            add_to_collection(*point_collection, current_node, current_time, rect.end_of_time(), current_values, rect);

            // reset values
            for (const auto &column : measurement_columns) {
                current_values[column] = std::numeric_limits<double>::quiet_NaN();
            }
        } else if (row.time > current_time) {
            add_to_collection(*point_collection, current_node, current_time, row.time, current_values, rect);
        }

        for (const auto &value : row.measurements) {
            current_values[value.first] = value.second;
        }

        current_node = row.node;
        current_time = row.time;
    });

    add_to_collection(*point_collection, current_node, current_time, rect.end_of_time(), current_values, rect);

    return point_collection;
}

auto Natur40SourceOperator::table_query(const std::string &table,
                                        const std::vector<std::string> &columns,
                                        double time_start,
                                        double time_end) -> std::string {
    assert(!columns.empty());

    std::map<std::string, std::string> replacements{
            {"@table@",      table},
            {"@columns@",    std::accumulate(
                    std::next(columns.begin()), // start with second element
                    columns.end(),
                    columns[0], // initialize with first element
                    [](std::string a, std::string b) {
                        return a + ", " + b;
                    })
            },
            {"@time_start@", "to_timestamp(" + std::to_string(time_start) + ")"},
            {"@time_end@",   "to_timestamp(" + std::to_string(time_end) + ")"},
    };

    const std::string query_template = R"(
        SELECT
            EXTRACT(EPOCH FROM time_start) AS time_start,
            EXTRACT(EPOCH FROM time_end) AS time_end,
            node,
            @columns@
        FROM (
            SELECT
                time as time_start,
                COALESCE(
                    LEAD(time) OVER (PARTITION BY node
                                     ORDER BY time ASC
                                     ROWS BETWEEN CURRENT ROW AND 1 FOLLOWING),
                    'infinity'
                ) AS time_end,
            node,
            @columns@
            FROM @table@
        ) t
        WHERE (time_start, time_end) OVERLAPS (@time_start@, @time_end@)
        ORDER BY node, time_start ASC;
    )";

    return parse_query(query_template, replacements);
}

auto Natur40SourceOperator::parse_query(const std::string &query_template,
                                        const std::map<std::string, std::string> &replacements) -> std::string {
    std::stringstream output;

    std::stringstream buffer;
    char last_char = '\n';
    enum {
        CHAR,
        BUFFER,
    } state = CHAR;

    for (const auto c : query_template) {
        switch (state) {
            case CHAR:
                if (c == '@') {
                    buffer << c;
                    state = BUFFER;
                } else {
                    if (c != ' ' || last_char != ' ') {
                        output << c;
                    }
                }
                break;
            case BUFFER:
                buffer << c;
                if (c == '@') {
                    state = CHAR;
                    output << replacements.at(buffer.str());
                    buffer.str("");
                }
                break;
        }
        last_char = c;
    }

    return output.str();
}

auto
Natur40SourceOperator::get_table_name(const std::string &sensor_type) -> const std::string {
    // TODO: grab from postgres?

    if (sensor_type == "location") {
        return "locations";
    } else if (sensor_type == "light") {
        return "lights";
    } else if (sensor_type == "pressure") {
        return "pressures";
    } else if (sensor_type == "temperature") {
        return "temperatures";
    } else if (sensor_type == "image") {
        return "images";
    } else {
        throw OperatorException {"natur40_source: Unkown sensor type `" + sensor_type + "`"};
    }
}

auto
Natur40SourceOperator::get_columns_for_table(const std::string &table_name) -> const std::vector<std::string> {
    std::vector<std::string> columns;

    // TODO: grab from postgres?

    if (table_name == "locations") {
        columns.emplace_back("longitude");
        columns.emplace_back("latitude");
        columns.emplace_back("altitude");
        columns.emplace_back("satellites");
    } else if (table_name == "lights") {
        columns.emplace_back("light");
    } else if (table_name == "pressures") {
        columns.emplace_back("pressure");
    } else if (table_name == "temperatures") {
        columns.emplace_back("temperature");
    } else if (table_name == "images") {
        columns.emplace_back("image");
    } else {
        throw OperatorException {"natur40_source: Unkown sensor type `" + table_name + "`"};
    }

    assert(!columns.empty());

    return columns;
}