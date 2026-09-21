#include "xyce_output_file.h"
#include "../core/step_information.h"
#include "../expression/expression.h"

XyceOutputFile::XyceOutputFile(XyceOutputFile&& other) noexcept :
    m_filename(std::move(other.m_filename)), m_title(std::move(other.m_title)), m_is_complex(other.m_is_complex), m_step_information(std::move(other.m_step_information)), m_plot_type(other.m_plot_type), m_abscissa_scale(other.m_abscissa_scale), m_expression_manager(std::move(other.m_expression_manager)), m_mapped_file(std::move(other.m_mapped_file)), m_metadata(std::move(other.m_metadata)), m_suggested_plots(std::move(other.m_suggested_plots)) {}

XyceOutputFile::XyceOutputFile(std::filesystem::path filename, std::string title, const bool is_complex, StepInformation&& step_info, const PlotType plot_type, const AbscissaScale abscissa_scale, ExpressionManager&& expression_manager, std::unique_ptr<MappedFile>&& mapped_file, std::vector<std::vector<std::string>> suggested_plots, std::unordered_map<std::string, std::string> metadata) :
    m_filename(std::move(filename)), m_title(std::move(title)), m_is_complex(is_complex), m_step_information(std::move(step_info)), m_plot_type(plot_type), m_abscissa_scale(abscissa_scale), m_expression_manager(std::move(expression_manager)), m_mapped_file(std::move(mapped_file)), m_metadata(std::move(metadata)), m_suggested_plots(std::move(suggested_plots)) {}

XyceOutputFile::~XyceOutputFile() = default;

XyceOutputFile& XyceOutputFile::operator=(XyceOutputFile&& other) noexcept {
    // move all field values to this instance
    m_filename = std::move(other.m_filename);
    m_title = std::move(other.m_title);
    m_is_complex = other.m_is_complex;
    m_step_information = std::move(other.m_step_information);
    m_plot_type = other.m_plot_type;
    m_abscissa_scale = other.m_abscissa_scale;
    m_expression_manager = std::move(other.m_expression_manager);
    m_mapped_file = std::move(other.m_mapped_file);
    m_metadata = std::move(other.m_metadata);
    m_suggested_plots = std::move(other.m_suggested_plots);
    // exit
    return *this;
}

const std::filesystem::path& XyceOutputFile::filename() const {
    // return filename
    return m_filename;
}

const std::string& XyceOutputFile::title() const {
    // return title
    return m_title;
}

bool XyceOutputFile::is_complex() const {
    // return complex status
    return m_is_complex;
}

const StepInformation& XyceOutputFile::step_information() const {
    // return step information
    return m_step_information;
}

Expression<double>& XyceOutputFile::abscissa() { return m_expression_manager.abscissa(); }

AbscissaScale XyceOutputFile::abscissa_scale() const {
    // return scale
    return m_abscissa_scale;
}

PlotType XyceOutputFile::plot_type() const {
    // return plot type
    return m_plot_type;
}

ExpressionManager& XyceOutputFile::expression_manager() {
    // return manager
    return m_expression_manager;
}

const std::unordered_map<std::string, std::string>& XyceOutputFile::metadata() const {
    // return metadata
    return m_metadata;
}

const std::vector<std::vector<std::string>>& XyceOutputFile::suggested_plots() const { return m_suggested_plots; }

void XyceOutputFile::set_title(std::string title) { m_title = std::move(title); }

void XyceOutputFile::set_plot_type(const PlotType plot_type) { m_plot_type = plot_type; }
