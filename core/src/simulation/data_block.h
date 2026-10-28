#pragma once

#include <string>
#include <vector>

// data block class — parses and serializes Xyce .DATA/.ENDDATA table blocks
class DataBlock
{
public:
    // construct a data block instance from individual fields
    DataBlock(std::string name, std::vector<std::string> parameters, std::vector<std::vector<std::string>> records);

    // parse all .DATA/.ENDDATA blocks from the directive list
    [[nodiscard]] static std::vector<DataBlock> from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives() const;

    // equality operator
    [[nodiscard]] bool operator==(const DataBlock& other) const;

    // table name
    std::string name;
    // column header names
    std::vector<std::string> parameters;
    // data records (rows), each row is a vector of string values
    std::vector<std::vector<std::string>> records;

private:
    // check if a string represents a numeric value (including spice scale suffixes)
    [[nodiscard]] static bool is_number(const std::string& s);
};
