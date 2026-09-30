#include <array>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <slint.h>

#include <main_window.h>

#include "../core/util.h"
#include "../simulation/option_parameters.h"
#include "main_window_view_def.h"
#include "options_dialog_view.h"

namespace options_dialog_view
{
    namespace
    {
        // number of managed packages shown by the panel
        constexpr size_t PACKAGE_COUNT = 16;

        // one window row-model setter, bound to the window at construction
        using RowSetter = std::function<void(const std::shared_ptr<slint::Model<main_window::OptionRow>>&)>;

        // the OptionParameters member for each package, in the panel's fixed order
        using PackageMember = std::map<std::string, std::string> OptionParameters::*;

        constexpr std::array<PackageMember, PACKAGE_COUNT> PACKAGE_MEMBERS = {
            &OptionParameters::device, &OptionParameters::timeint, &OptionParameters::nonlin, &OptionParameters::nonlin_tran, &OptionParameters::linsol, &OptionParameters::linsol_ac, &OptionParameters::loca, &OptionParameters::parser, &OptionParameters::diagnostic, &OptionParameters::dist, &OptionParameters::measure, &OptionParameters::fft, &OptionParameters::output, &OptionParameters::restart, &OptionParameters::samples, &OptionParameters::embeddedsamples,
        };

        // copy the current rows out of a host-owned model
        [[nodiscard]] std::vector<main_window::OptionRow> read_rows(const std::shared_ptr<slint::VectorModel<main_window::OptionRow>>& model) {
            std::vector<main_window::OptionRow> rows;
            rows.reserve(model->row_count());
            for (size_t i = 0; i < model->row_count(); ++i)
                if (const auto row = model->row_data(i))
                    rows.push_back(*row);
            return rows;
        }
    } // namespace

    const std::vector<std::string>& option_package_catalog(size_t package_index) {
        // keys extracted from the Xyce Reference Guide LaTeX sources (doc/Reference_Guide), one entry per package in the panel's fixed order
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
        // throw on an unknown package index
        return CATALOGS.at(package_index);
    }

    std::map<std::string, std::string>& package_options(OptionParameters& options, size_t package_index) {
        // resolve the package member, throwing on an unknown package index
        return options.*(PACKAGE_MEMBERS.at(package_index));
    }

    const std::map<std::string, std::string>& package_options(const OptionParameters& options, size_t package_index) {
        // resolve the package member, throwing on an unknown package index
        return options.*(PACKAGE_MEMBERS.at(package_index));
    }

    std::vector<main_window::OptionRow> build_option_rows(const std::vector<std::string>& catalog, const std::map<std::string, std::string>& loaded) {
        std::vector<main_window::OptionRow> rows;
        rows.reserve(catalog.size() + loaded.size());
        // place every documented key first, pre-filled from the netlist when present
        for (const auto& key : catalog) {
            const auto found = loaded.find(key);
            const bool present = found != loaded.end();
            // absent keys render as an empty row while present bare flags keep the flag marker
            const std::string value = present ? found->second : "";
            rows.push_back(main_window::OptionRow{slint::SharedString(key), slint::SharedString(value), present && value.empty()});
        }
        // remember which keys already have a row so netlist-only keys are not duplicated
        const std::set<std::string> placed(catalog.begin(), catalog.end());
        // append netlist keys the catalog does not document so no loaded option is hidden
        for (const auto& [key, value] : loaded)
            if (placed.count(key) == 0)
                rows.push_back(main_window::OptionRow{slint::SharedString(key), slint::SharedString(value), value.empty()});
        return rows;
    }

    std::map<std::string, std::string> apply_option_rows(const std::vector<main_window::OptionRow>& rows) {
        std::map<std::string, std::string> options;
        for (const auto& row : rows) {
            // keys reach the dialog uppercased from the catalog and the netlist parser
            const std::string key = to_upper(trim(std::string(row.key)));
            // values are written verbatim apart from surrounding whitespace
            const std::string value = trim(std::string(row.value));
            // a row without a key cannot form an option and is skipped
            if (key.empty())
                continue;
            // a non-empty value writes KEY=VALUE
            if (!value.empty())
                options[key] = value;
            // an empty row keeps only keys the netlist carried as bare flags
            else if (row.flag)
                options[key] = "";
        }
        return options;
    }

    struct OptionsDialogView::Impl
    {
        // the main window handle; the panel is an inline child of the window, so all interaction goes through the window's properties and callbacks
        slint::ComponentHandle<main_window::MainWindow> window;

        // presenter notified with the edited options
        MainWindowViewDefEvents* handler = nullptr;

        // notified on both accept and cancel, after the panel is hidden; the caller releases the modal state from here
        std::function<void()> on_closed;

        // one host-owned row model per package, in the panel's fixed order
        std::array<std::shared_ptr<slint::VectorModel<main_window::OptionRow>>, PACKAGE_COUNT> models;

        Impl(slint::ComponentHandle<main_window::MainWindow> w) :
            window(w) {
            // the window row-model setters in the panel's fixed package order
            const std::array<RowSetter, PACKAGE_COUNT> setters = {
                [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_device_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_timeint_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_nonlin_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_nonlin_tran_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_linsol_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_linsol_ac_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_loca_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_parser_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_diagnostic_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_dist_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_measure_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_fft_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_output_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_restart_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_samples_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_embeddedsamples_rows(rows); },
            };
            // create one empty model per package and publish it to the matching window property
            for (size_t i = 0; i < PACKAGE_COUNT; ++i) {
                models[i] = std::make_shared<slint::VectorModel<main_window::OptionRow>>();
                setters[i](models[i]);
            }
            // wire the forwarded callbacks from the inline panel to this view
            window->on_options_accepted([this] { accept(); });
            window->on_options_dismissed([this] { dismiss(); });
            window->on_options_row_edited([this](int package_index, int row_index, main_window::OptionRow row) { edit(package_index, row_index, row); });
        }

        void seed(size_t package_index, const OptionParameters& current) {
            // build the rows from the package catalog and the current option values
            auto rows = build_option_rows(option_package_catalog(package_index), package_options(current, package_index));
            // replace the model contents in place so the window property keeps the same model
            models[package_index]->set_vector(std::move(rows));
        }

        void edit(int package_index, int row_index, const main_window::OptionRow& row) {
            // ignore edits for packages outside the panel
            if (package_index < 0 || static_cast<size_t>(package_index) >= PACKAGE_COUNT)
                return;
            // ignore rows outside the current model
            if (row_index < 0 || static_cast<size_t>(row_index) >= models[package_index]->row_count())
                return;
            // commit the edited row into the host-owned model
            models[package_index]->set_row_data(static_cast<size_t>(row_index), row);
        }

        void accept() {
            // fold the edited row models back into one OptionParameters value
            OptionParameters options({}, {}, {}, {}, {});
            for (size_t i = 0; i < PACKAGE_COUNT; ++i)
                package_options(options, i) = apply_option_rows(read_rows(models[i]));
            // hide the panel before delivering the result
            window->set_options_visible(false);
            // release the modal state held by the caller
            if (on_closed)
                on_closed();
            // deliver the edited options to the presenter
            if (handler != nullptr)
                handler->on_options_dialog_result(options);
        }

        void dismiss() {
            // hide the panel
            window->set_options_visible(false);
            // release the modal state held by the caller
            if (on_closed)
                on_closed();
        }
    };

    OptionsDialogView::OptionsDialogView(slint::ComponentHandle<main_window::MainWindow> main_window) :
        m_impl(std::make_unique<Impl>(main_window)) {}

    OptionsDialogView::~OptionsDialogView() = default;

    void OptionsDialogView::show(const OptionParameters& current, MainWindowViewDefEvents& handler, const std::function<void()>& on_closed) {
        // remember the result destination for the panel lifetime
        m_impl->handler = &handler;
        // remember the close notification for this show
        m_impl->on_closed = on_closed;
        // rebuild every package model from its catalog and the current options
        for (size_t i = 0; i < PACKAGE_COUNT; ++i)
            m_impl->seed(i, current);
        // show the inline panel
        m_impl->window->set_options_visible(true);
    }
} // namespace options_dialog_view
