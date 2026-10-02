#include <array>
#include <map>
#include <string>
#include <vector>

#include "../core/util.h"
#include "option_catalog.h"

namespace
{
    // the OptionParameters member for each package, indexed by OptionPackage
    using PackageMember = std::map<std::string, std::string> OptionParameters::*;

    constexpr std::array<PackageMember, OPTION_PACKAGE_COUNT> PACKAGE_MEMBERS = {
        &OptionParameters::device, &OptionParameters::timeint, &OptionParameters::nonlin, &OptionParameters::nonlin_tran, &OptionParameters::linsol, &OptionParameters::linsol_ac, &OptionParameters::loca, &OptionParameters::parser, &OptionParameters::diagnostic, &OptionParameters::dist, &OptionParameters::measure, &OptionParameters::fft, &OptionParameters::output, &OptionParameters::restart, &OptionParameters::samples, &OptionParameters::embeddedsamples,
    };

    // editing metadata keyed by uppercase option key, one map per OptionPackage in enum order
    using PackageKeyInfo = std::map<std::string, OptionKeyInfo>;

    const std::array<PackageKeyInfo, OPTION_PACKAGE_COUNT>& option_key_infos() {
        static const std::array<PackageKeyInfo, OPTION_PACKAGE_COUNT> INFOS = {{
            {
                // device package metadata
                {"B3SOIGMINSCALING", {{"0", "1"}, "1"}}, {"B3SOIVOLTLIM", {{"0", "1"}, "1"}}, {"DEBUGLEVEL", {{}, "1"}}, {"DEBUGMAXTIME", {{}, "100.0"}}, {"DEBUGMAXTIMESTEP", {{}, "65536"}}, {"DEBUGMINTIME", {{}, "0.0"}}, {"DEBUGMINTIMESTEP", {{}, "0"}}, {"DEFAD", {{}, "0.0"}}, {"DEFAS", {{}, "0.0"}}, {"DEFL", {{}, "1.0E-4"}}, {"DEFW", {{}, "1.0E-4"}}, {"DIGINITSTATE", {{}, "3"}}, {"GMIN", {{}, "1.0E-12"}}, {"ICFAC", {{}, "10000.0"}}, {"LENGTH0", {{}, "5.0e-6"}}, {"MAXTIMESTEP", {{}, "1.0E+99"}}, {"MINCAP", {{}, "0.0"}}, {"MINRES", {{}, "0.0"}}, {"NUMJAC", {{"0", "1"}, "0"}}, {"RCCONST", {{}, "1e-9"}}, {"SMOOTHBSRC", {{"0", "1"}, "0"}}, {"TEMP", {{}, "27.0"}}, {"TNOM", {{}, "27.0"}}, {"TOX0", {{}, "6.0e-8"}}, {"VDSSCALEMIN", {{}, "0.3"}}, {"VGSTCONST", {{}, "4.5"}}, {"VOLTLIM", {{"0", "1"}, "1"}}, {"WIDTH0", {{}, "200.0e-6"}},
            },
            {
                // timeint package metadata
                {"ABSTOL", {{}, "1.0E-06"}}, {"BPENABLE", {{"0", "1"}, "1"}}, {"DELMAX", {{}, "1e99"}}, {"ERROPTION", {{"0", "1"}, "0"}}, {"MASKIVARS", {{"0", "1"}, "0"}}, {"MAXORD", {{}, "2"}}, {"METHOD", {{"trap", "7", "gear", "8"}, "trap"}}, {"MINORD", {{}, "1"}}, {"MINTIMESTEPSBP", {{}, "10"}}, {"NEWBPSTEPPING", {{"0", "1"}, "1"}}, {"NEWLTE", {{"0", "1", "2", "3"}, "1"}}, {"NLMAX", {{}, "8"}}, {"NLMIN", {{}, "3"}}, {"NLNEARCONV", {{"0", "1"}, "0"}}, {"NLSMALLUPDATE", {{"0", "1"}, "1"}}, {"RELTOL", {{}, "1.0E-03"}}, {"RESETTRANNLS", {{"0", "1"}, "1"}}, {"RESTARTSTEPSCALE", {{}, "0.005"}}, {"TIMESTEPSREVERSAL", {{"0", "1"}, "0"}},
            },
            {
                // nonlin package metadata
                {"ABSTOL", {{}, "1.0E-12"}}, {"AZ_TOL", {{}, "1.0E-12"}}, {"CONTINUATION", {{"0", "1", "2", "3", "34", "35", "mos", "gmin", "sourcestep", "sourcestep2"}, "0"}}, {"DEBUGLEVEL", {{}, "1"}}, {"DEBUGMAXTIME", {{}, "1.0E+99"}}, {"DEBUGMAXTIMESTEP", {{}, "99999999"}}, {"DEBUGMINTIME", {{}, "0.0"}}, {"DEBUGMINTIMESTEP", {{}, "0"}}, {"DELTAXTOL", {{}, "1.0"}}, {"IN_FORCING", {{"0", "1"}, "0"}}, {"MAXSEARCHSTEP", {{}, "2"}}, {"MAXSTEP", {{}, "200"}}, {"NLSTRATEGY", {{"0", "1", "2"}, "0"}}, {"NOX", {{"0", "1"}, "1"}}, {"RECOVERYSTEP", {{}, "1.0"}}, {"RECOVERYSTEPTYPE", {{"0", "1"}, "0"}}, {"RELTOL", {{}, "1.0E-03"}}, {"RHSTOL", {{}, "1.0E-06"}}, {"SEARCHMETHOD", {{"0", "1", "2", "3", "4"}, "0"}}, {"SMALLUPDATETOL", {{}, "1.0E-06"}},
            },
            {
                // nonlin-tran package metadata
                {"ABSTOL", {{}, "1.0E-06"}}, {"AZ_TOL", {{}, "1.0E-12"}}, {"CONTINUATION", {{"0", "1", "2", "3", "34", "35", "mos", "gmin", "sourcestep", "sourcestep2"}, "0"}}, {"DEBUGLEVEL", {{}, "1"}}, {"DEBUGMAXTIME", {{}, "1.0E+99"}}, {"DEBUGMAXTIMESTEP", {{}, "99999999"}}, {"DEBUGMINTIME", {{}, "0.0"}}, {"DEBUGMINTIMESTEP", {{}, "0"}}, {"DELTAXTOL", {{}, "0.33"}}, {"IN_FORCING", {{"0", "1"}, "0"}}, {"MAXSEARCHSTEP", {{}, "2"}}, {"MAXSTEP", {{}, "20"}}, {"NLSTRATEGY", {{"0", "1", "2"}, "0"}}, {"NOX", {{"0", "1"}, "0"}}, {"RECOVERYSTEP", {{}, "1.0"}}, {"RECOVERYSTEPTYPE", {{"0", "1"}, "0"}}, {"RELTOL", {{}, "1.0E-02"}}, {"RHSTOL", {{}, "1.0E-02"}}, {"SEARCHMETHOD", {{"0", "1", "2", "3", "4"}, "0"}}, {"SMALLUPDATETOL", {{}, "1.0E-06"}},
            },
            {
                // linsol package metadata
                {"ADAPTIVE_SOLVE", {{"0", "1"}, "0"}}, {"AZ_ATHRESH", {{}, "1.0E-04"}}, {"AZ_CONV", {{}, "AZ_r0"}}, {"AZ_DIAGNOSTICS", {{}, "AZ_none"}}, {"AZ_DROP", {{}, "1.0E-03"}}, {"AZ_ILUT_FILL", {{}, "2.0"}}, {"AZ_KEEP_INFO", {{}, "AZ_true"}}, {"AZ_KSPACE", {{}, "50"}}, {"AZ_MAX_ITER", {{}, "200"}}, {"AZ_ORTHOG", {{}, "AZ_modified"}}, {"AZ_OVERLAP", {{}, "0"}}, {"AZ_PRECOND", {{}, "AZ_dom_decomp"}}, {"AZ_PRE_CALC", {{}, "AZ_recalc"}}, {"AZ_REORDER", {{}, "AZ_none"}}, {"AZ_RTHRESH", {{}, "1.0001"}}, {"AZ_SCALING", {{}, "AZ_none"}}, {"AZ_SOLVER", {{}, "AZ_gmres"}}, {"AZ_SUBDOMAIN_SOLVE", {{}, "AZ_ilut"}}, {"AZ_TOL", {{}, "1.0E-9"}}, {"IFPACK_TYPE", {{"Amesos", "ILU", "ILUT"}, "Amesos"}}, {"OUTPUT_BASE_LS", {{"0", "1"}, "0"}}, {"OUTPUT_FAILED_LS", {{"0", "1"}, "0"}}, {"OUTPUT_LS", {{"0", "1"}, "0"}}, {"PREC_TYPE", {{}, "Ifpack"}}, {"SHYLU_RTHRESH", {{}, "1.0E-03"}}, {"TR_AMD", {{"0", "1"}, "0"}}, {"TR_GLOBAL_BTF", {{"0", "1"}, "0"}}, {"TR_PARTITION", {{"0", "1"}, "0"}}, {"TR_PARTITION_TYPE", {{}, "HYPERGRAPH"}}, {"TR_REINDEX", {{"0", "1"}, "1"}}, {"TR_SINGLETON_FILTER", {{"0", "1"}, "0"}}, {"TR_SOLVERMAP", {{"0", "1"}, "1"}}, {"TYPE", {{"KLU", "KSparse", "SuperLU", "AztecOO", "Belos", "ShyLU"}, "KLU"}}, {"USE_AZTEC_PRECOND", {{"0", "1"}, "0"}}, {"USE_IFPACK_FACTORY", {{"0", "1"}, "0"}},
            },
            {
                // linsol-ac package metadata
                {"ADAPTIVE_SOLVE", {{"0", "1"}, "0"}}, {"AZ_ATHRESH", {{}, "1.0E-04"}}, {"AZ_CONV", {{}, "AZ_r0"}}, {"AZ_DIAGNOSTICS", {{}, "AZ_none"}}, {"AZ_DROP", {{}, "1.0E-03"}}, {"AZ_ILUT_FILL", {{}, "2.0"}}, {"AZ_KEEP_INFO", {{}, "AZ_true"}}, {"AZ_KSPACE", {{}, "50"}}, {"AZ_MAX_ITER", {{}, "200"}}, {"AZ_ORTHOG", {{}, "AZ_modified"}}, {"AZ_OVERLAP", {{}, "0"}}, {"AZ_PRECOND", {{}, "AZ_dom_decomp"}}, {"AZ_PRE_CALC", {{}, "AZ_recalc"}}, {"AZ_REORDER", {{}, "AZ_none"}}, {"AZ_RTHRESH", {{}, "1.0001"}}, {"AZ_SCALING", {{}, "AZ_none"}}, {"AZ_SOLVER", {{}, "AZ_gmres"}}, {"AZ_SUBDOMAIN_SOLVE", {{}, "AZ_ilut"}}, {"AZ_TOL", {{}, "1.0E-9"}}, {"IFPACK_TYPE", {{"Amesos", "ILU", "ILUT"}, "Amesos"}}, {"OUTPUT_BASE_LS", {{"0", "1"}, "0"}}, {"OUTPUT_FAILED_LS", {{"0", "1"}, "0"}}, {"OUTPUT_LS", {{"0", "1"}, "0"}}, {"PREC_TYPE", {{}, "Ifpack"}}, {"SHYLU_RTHRESH", {{}, "1.0E-03"}}, {"TR_AMD", {{"0", "1"}, "0"}}, {"TR_GLOBAL_BTF", {{"0", "1"}, "0"}}, {"TR_PARTITION", {{"0", "1"}, "0"}}, {"TR_PARTITION_TYPE", {{}, "HYPERGRAPH"}}, {"TR_REINDEX", {{"0", "1"}, "1"}}, {"TR_SINGLETON_FILTER", {{"0", "1"}, "0"}}, {"TR_SOLVERMAP", {{"0", "1"}, "1"}}, {"TYPE", {{"KLU", "KSparse", "SuperLU", "AztecOO", "Belos", "ShyLU"}, "KLU"}}, {"USE_AZTEC_PRECOND", {{"0", "1"}, "0"}}, {"USE_IFPACK_FACTORY", {{"0", "1"}, "0"}},
            },
            {
                // loca package metadata
                {"AGGRESSIVENESS", {{}, "0.0"}},
                {"BIFPARAM", {{}, "VA:V0"}},
                {"CONPARAM", {{}, "VA:V0"}},
                {"INITIALSTEPSIZE", {{}, "1.0"}},
                {"INITIALVALUE", {{}, "0.0"}},
                {"MAXNLITERS", {{}, "20"}},
                {"MAXSTEPS", {{}, "20"}},
                {"MAXSTEPSIZE", {{}, "1.0E-4"}},
                {"MAXVALUE", {{}, "1.0E20"}},
                {"MINSTEPSIZE", {{}, "1.0E20"}},
                {"MINVALUE", {{}, "-1.0E20"}},
                {"PREDICTOR", {{"0", "1", "2", "3"}, "0"}},
                {"RESIDUALCONDUCTANCE", {{}, "0.0"}},
                {"STEPCONTROL", {{"0", "1"}, "0"}},
                {"STEPPER", {{"0", "1"}, "0"}},
            },
            {
                // parser package metadata
                {"MODEL_BINNING", {{"true", "false"}, "true"}},
                {"SCALE", {{}, "1.0"}},
            },
            {
                // diagnostic package metadata
                {"CURRENTLIMIT", {{}, "0.0"}},
                {"DISCLIMIT", {{}, "0.0"}},
                {"EXTREMA", {{"true", "false"}, "true"}},
                {"EXTREMALIMIT", {{}, "0.0"}},
                {"VOLTAGELIMIT", {{}, "0.0"}},
            },
            {
                // dist package metadata
                {"STRATEGY", {{"0", "1", "2"}, "0"}},
            },
            {
                // measure package metadata
                {"DEFAULT_VAL", {{}, "-1"}},
                {"MEASDGT", {{}, "6"}},
                {"MEASFAIL", {{"0", "1"}, "1"}},
                {"MEASOUT", {{"0", "1"}, "1"}},
                {"MEASPRINT", {{"ALL", "STDOUT", "NONE"}, "ALL"}},
                {"USE_CONT_FILES", {{"0", "1"}, "1"}},
                {"USE_LTTM", {{"0", "1"}, "0"}},
            },
            {
                // fft package metadata
                {"FFTOUT", {{"0", "1"}, "0"}},
                {"FFT_ACCURATE", {{"0", "1"}, "1"}},
                {"FFT_MODE", {{"0", "1"}, "0"}},
            },
            {
                // output package metadata
                {"ADD_STEPNUM_COL", {{"true", "false"}, "false"}},
                {"PHASE_OUTPUT_RADIANS", {{"true", "false"}, "false"}},
                {"PRINTHEADER", {{"true", "false"}, ""}},
                {"PRINTFOOTER", {{"true", "false"}, ""}},
                {"SNAPSHOTS", {{"true", "false"}, ""}},
            },
            {
                // restart package metadata
            },
            {
                // samples package metadata
                {"NUMSAMPLES", {{}, "0"}},
                {"SAMPLE_TYPE", {{"MC", "LHS"}, "MC"}},
            },
            {
                // embeddedsamples package metadata
                {"NUMSAMPLES", {{}, "0"}},
                {"SAMPLE_TYPE", {{"MC", "LHS"}, "MC"}},
            },
        }};
        return INFOS;
    }
} // namespace

const std::vector<std::string>& option_package_catalog(OptionPackage package) {
    // keys extracted from the Xyce Reference Guide LaTeX sources (doc/Reference_Guide), one entry per package in the OptionPackage order
    static const std::vector<std::vector<std::string>> CATALOGS = {
        // device package keys (28)
        {"DEFAD", "DEFAS", "DEFL", "DEFW", "DIGINITSTATE", "GMIN", "MINRES", "MINCAP", "TEMP", "TNOM", "NUMJAC", "VOLTLIM", "B3SOIVOLTLIM", "B3SOIGMINSCALING", "ICFAC", "MAXTIMESTEP", "SMOOTHBSRC", "RCCONST", "VDSSCALEMIN", "VGSTCONST", "LENGTH0", "WIDTH0", "TOX0", "DEBUGLEVEL", "DEBUGMINTIMESTEP", "DEBUGMAXTIMESTEP", "DEBUGMINTIME", "DEBUGMAXTIME"},
        // time integration package keys (23)
        {"METHOD", "RELTOL", "ABSTOL", "RESTARTSTEPSCALE", "NLNEARCONV", "NLSMALLUPDATE", "RESETTRANNLS", "MAXORD", "MINORD", "NEWLTE", "NEWBPSTEPPING", "MASKIVARS", "ERROPTION", "NLMIN", "NLMAX", "DELMAX", "MINTIMESTEPSBP", "TIMESTEPSREVERSAL", "DOUBLEDCOPSTEP", "BREAKPOINTS", "BPENABLE", "EXITTIME", "EXITSTEP"},
        // nonlinear solver package keys (20)
        {"NOX", "NLSTRATEGY", "SEARCHMETHOD", "CONTINUATION", "ABSTOL", "RELTOL", "DELTAXTOL", "RHSTOL", "SMALLUPDATETOL", "MAXSTEP", "MAXSEARCHSTEP", "IN_FORCING", "AZ_TOL", "RECOVERYSTEPTYPE", "RECOVERYSTEP", "DEBUGLEVEL", "DEBUGMINTIMESTEP", "DEBUGMAXTIMESTEP", "DEBUGMINTIME", "DEBUGMAXTIME"},
        // transient nonlinear solver package keys (20)
        {"NOX", "NLSTRATEGY", "SEARCHMETHOD", "CONTINUATION", "ABSTOL", "RELTOL", "DELTAXTOL", "RHSTOL", "SMALLUPDATETOL", "MAXSTEP", "MAXSEARCHSTEP", "IN_FORCING", "AZ_TOL", "RECOVERYSTEPTYPE", "RECOVERYSTEP", "DEBUGLEVEL", "DEBUGMINTIMESTEP", "DEBUGMAXTIMESTEP", "DEBUGMINTIME", "DEBUGMAXTIME"},
        // linear solver package keys (36)
        {"TYPE", "PREC_TYPE", "USE_AZTEC_PRECOND", "USE_IFPACK_FACTORY", "IFPACK_TYPE", "SHYLU_RTHRESH", "TR_PARTITION", "TR_PARTITION_TYPE", "TR_SINGLETON_FILTER", "TR_AMD", "TR_GLOBAL_BTF", "TR_REINDEX", "TR_SOLVERMAP", "ADAPTIVE_SOLVE", "AZ_MAX_ITER", "AZ_PRECOND", "AZ_SOLVER", "AZ_CONV", "AZ_PRE_CALC", "AZ_KEEP_INFO", "AZ_ORTHOG", "AZ_SUBDOMAIN_SOLVE", "AZ_ILUT_FILL", "AZ_DROP", "AZ_REORDER", "AZ_SCALING", "AZ_KSPACE", "AZ_TOL", "AZ_OUTPUT", "AZ_DIAGNOSTICS", "AZ_OVERLAP", "AZ_RTHRESH", "AZ_ATHRESH", "OUTPUT_LS", "OUTPUT_BASE_LS", "OUTPUT_FAILED_LS"},
        // AC linear solver package keys (36)
        {"TYPE", "PREC_TYPE", "USE_AZTEC_PRECOND", "USE_IFPACK_FACTORY", "IFPACK_TYPE", "SHYLU_RTHRESH", "TR_PARTITION", "TR_PARTITION_TYPE", "TR_SINGLETON_FILTER", "TR_AMD", "TR_GLOBAL_BTF", "TR_REINDEX", "TR_SOLVERMAP", "ADAPTIVE_SOLVE", "AZ_MAX_ITER", "AZ_PRECOND", "AZ_SOLVER", "AZ_CONV", "AZ_PRE_CALC", "AZ_KEEP_INFO", "AZ_ORTHOG", "AZ_SUBDOMAIN_SOLVE", "AZ_ILUT_FILL", "AZ_DROP", "AZ_REORDER", "AZ_SCALING", "AZ_KSPACE", "AZ_TOL", "AZ_OUTPUT", "AZ_DIAGNOSTICS", "AZ_OVERLAP", "AZ_RTHRESH", "AZ_ATHRESH", "OUTPUT_LS", "OUTPUT_BASE_LS", "OUTPUT_FAILED_LS"},
        // continuation and bifurcation tracking package keys (15)
        {"STEPPER", "PREDICTOR", "STEPCONTROL", "CONPARAM", "INITIALVALUE", "MINVALUE", "MAXVALUE", "BIFPARAM", "MAXSTEPS", "MAXNLITERS", "INITIALSTEPSIZE", "MINSTEPSIZE", "MAXSTEPSIZE", "AGGRESSIVENESS", "RESIDUALCONDUCTANCE"},
        // parser package keys (2)
        {"MODEL_BINNING", "SCALE"},
        // diagnostic package keys (6)
        {"EXTREMA", "EXTREMALIMIT", "VOLTAGELIMIT", "CURRENTLIMIT", "DISCLIMIT", "DIAGFILENAME"},
        // distribution package keys (1)
        {"STRATEGY"},
        // measure package keys (7)
        {"DEFAULT_VAL", "MEASDGT", "MEASFAIL", "MEASOUT", "MEASPRINT", "USE_CONT_FILES", "USE_LTTM"},
        // FFT package keys (3)
        {"FFT_ACCURATE", "FFTOUT", "FFT_MODE"},
        // output package keys (7)
        {"INITIAL_INTERVAL", "OUTPUTTIMEPOINTS", "PRINTHEADER", "PRINTFOOTER", "SNAPSHOTS", "ADD_STEPNUM_COL", "PHASE_OUTPUT_RADIANS"},
        // restart package keys (5)
        {"PACK", "JOB", "INITIAL_INTERVAL", "FILE", "START_TIME"},
        // sampling package keys (13)
        {"NUMSAMPLES", "SAMPLE_TYPE", "OUTPUTS", "MEASURES", "COVMATRIX", "SEED", "OUTPUT_SAMPLE_STATS", "REGRESSION_PCE", "PROJECTION_PCE", "RESAMPLE", "OUTPUT_PCE_COEFFS", "SPARSE_GRID", "STDOUTPUT"},
        // embedded sampling package keys (12)
        {"NUMSAMPLES", "SAMPLE_TYPE", "OUTPUTS", "COVMATRIX", "SEED", "OUTPUT_SAMPLE_STATS", "REGRESSION_PCE", "PROJECTION_PCE", "RESAMPLE", "OUTPUT_PCE_COEFFS", "SPARSE_GRID", "STDOUTPUT"},
    };
    // throw on a package outside the enum range
    return CATALOGS.at(static_cast<size_t>(package));
}

std::map<std::string, std::string>& package_options(OptionParameters& options, OptionPackage package) {
    // resolve the package member, throwing on a package outside the enum range
    return options.*(PACKAGE_MEMBERS.at(static_cast<size_t>(package)));
}

const std::map<std::string, std::string>& package_options(const OptionParameters& options, OptionPackage package) {
    // resolve the package member, throwing on a package outside the enum range
    return options.*(PACKAGE_MEMBERS.at(static_cast<size_t>(package)));
}

OptionKeyInfo option_key_info(OptionPackage package, const std::string& key) {
    // throw on a package outside the enum range
    const auto& infos = option_key_infos().at(static_cast<size_t>(package));
    // report empty metadata for keys without editing information
    const auto found = infos.find(to_upper(key));
    if (found == infos.end())
        return {};
    return found->second;
}
