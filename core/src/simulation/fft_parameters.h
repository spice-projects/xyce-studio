#pragma once

#include <optional>
#include <string>

// fft parameters class — parses and serializes Xyce .FFT directives
class FftParameters
{
public:
    // construct an fft parameters instance from individual fields
    FftParameters(std::string output_variable, std::string np, std::string window, std::string alfa, std::string fft_format, std::string start, std::string stop, std::string freq, std::string fmin, std::string fmax);

    // parse a .FFT directive string into a FftParameters instance;
    // returns nullopt when the string is not a valid .FFT statement
    [[nodiscard]] static std::optional<FftParameters> from_xyce_statement(const std::string& fft_statement);

    // serialize this instance back to a .FFT directive string
    [[nodiscard]] std::string to_xyce_statement() const;

    // equality operator
    [[nodiscard]] bool operator==(const FftParameters& other) const;

    // required positional output variable
    std::string output_variable;
    // number of points (NP); empty when not specified
    std::string np;
    // window function (e.g. "HANN", "RECT"); empty when not specified
    std::string window;
    // window shape parameter (ALFA); empty when not specified
    std::string alfa;
    // output format ("NORM" or "UNORM"); empty when not specified
    std::string fft_format;
    // start time for the FFT window; empty when not specified
    std::string start;
    // stop time for the FFT window; empty when not specified
    std::string stop;
    // fundamental frequency override (FREQ); empty when not specified
    std::string freq;
    // minimum frequency for output (FMIN); empty when not specified
    std::string fmin;
    // maximum frequency for output (FMAX); empty when not specified
    std::string fmax;
};
