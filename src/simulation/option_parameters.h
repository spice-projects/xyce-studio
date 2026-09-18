#pragma once

#include <map>
#include <string>
#include <vector>

struct NetlistTopology;

// option parameters class — parses and serializes Xyce .OPTIONS directives
class OptionParameters
{
public:
    // construct an option parameters instance from individual fields
    OptionParameters(std::map<std::string, std::string> device, std::map<std::string, std::string> timeint, std::map<std::string, std::string> nonlin, std::map<std::string, std::string> linsol, std::map<std::string, std::string> fft, std::map<std::string, std::string> diagnostic = {}, std::map<std::string, std::string> parser = {}, std::map<std::string, std::string> linsol_ac = {}, std::map<std::string, std::string> loca = {}, std::map<std::string, std::string> dist = {}, std::map<std::string, std::string> measure = {}, std::map<std::string, std::string> nonlin_tran = {}, std::map<std::string, std::string> output = {}, std::map<std::string, std::string> restart = {});

    // parse all .OPTIONS directives into an OptionParameters instance
    [[nodiscard]] static OptionParameters from_xyce_directives(const std::vector<std::string>& directives);

    // serialize this instance to a list of Xyce directive strings
    [[nodiscard]] std::vector<std::string> to_xyce_directives(const NetlistTopology& topology) const;

    // equality operator
    [[nodiscard]] bool operator==(const OptionParameters& other) const;

    // top-level device options applied across simulations
    std::map<std::string, std::string> device;
    // transient integration control parameters
    std::map<std::string, std::string> timeint;
    // generic nonlinear solver parameters
    std::map<std::string, std::string> nonlin;
    // generic linear solver parameters
    std::map<std::string, std::string> linsol;
    // options controlling all .FFT statements (FFT_ACCURATE, FFTOUT, FFT_MODE)
    std::map<std::string, std::string> fft;
    // diagnostic output verbosity parameters (DEBUGLEVEL)
    std::map<std::string, std::string> diagnostic;
    // netlist parsing options (MODEL_BINNING, SCALE)
    std::map<std::string, std::string> parser;
    // AC-specific linear solver parameters (same option keys as LINSOL)
    std::map<std::string, std::string> linsol_ac;
    // continuation/bifurcation tracking parameters (used when NONLIN CONTINUATION > 0)
    std::map<std::string, std::string> loca;
    // parallel device distribution parameters (STRATEGY)
    std::map<std::string, std::string> dist;
    // measure output control parameters (MEASDGT, MEASFAIL, MEASOUT, MEASPRINT, etc.)
    std::map<std::string, std::string> measure;
    // transient nonlinear solver parameters (same option keys as NONLIN)
    std::map<std::string, std::string> nonlin_tran;
    // transient output control parameters (INITIAL_INTERVAL, OUTPUTTIMEPOINTS, PRINTHEADER, etc.)
    std::map<std::string, std::string> output;
    // checkpointing/restarting control parameters (PACK, JOB, FILE, START_TIME, INITIAL_INTERVAL)
    std::map<std::string, std::string> restart;
};
