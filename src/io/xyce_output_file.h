#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/step_information.h"
#include "../expression/expression.h"
#include "../expression/expression_manager.h"
#include "mapped_file.h"

enum class AbscissaScale
{
    LINEAR,
    DECADE,
    OCTAVE
};

enum class PlotType
{
    UNKNOWN,
    TRANSIENT,
    AC,
    DC,
    NOISE,
    DC_OPERATING_POINT,
    FFT,
    PCE
};

class XyceOutputFile
{
public:
    XyceOutputFile() = delete;

    XyceOutputFile(const XyceOutputFile&) = delete;

    XyceOutputFile(XyceOutputFile&&) noexcept;

    XyceOutputFile(std::filesystem::path filename, std::string title, bool is_complex, StepInformation&& step_info, PlotType plot_type, AbscissaScale abscissa_scale, ExpressionManager&& expression_manager, std::unique_ptr<MappedFile>&& mapped_file, std::vector<std::vector<std::string>> suggested_plots = {}, std::unordered_map<std::string, std::string> metadata = {});

    ~XyceOutputFile();

    XyceOutputFile& operator=(const XyceOutputFile&) = delete;

    XyceOutputFile& operator=(XyceOutputFile&&) noexcept;

    [[nodiscard]] const std::filesystem::path& filename() const;

    [[nodiscard]] const std::string& title() const;

    [[nodiscard]] bool is_complex() const;

    [[nodiscard]] const StepInformation& step_information() const;

    [[nodiscard]] Expression<double>& abscissa();

    [[nodiscard]] AbscissaScale abscissa_scale() const;

    [[nodiscard]] PlotType plot_type() const;

    [[nodiscard]] ExpressionManager& expression_manager();

    [[nodiscard]] const std::unordered_map<std::string, std::string>& metadata() const;

    [[nodiscard]] const std::vector<std::vector<std::string>>& suggested_plots() const;

    // set the title (used by the presenter for .prn outputs that carry no
    // analysis-type header in the file itself)
    void set_title(std::string title);

    // set the plot type (used by the presenter for .prn outputs)
    void set_plot_type(PlotType plot_type);

private:
    std::filesystem::path m_filename;
    std::string m_title;
    bool m_is_complex;
    StepInformation m_step_information;
    PlotType m_plot_type;
    AbscissaScale m_abscissa_scale;
    ExpressionManager m_expression_manager;

    std::unique_ptr<MappedFile> m_mapped_file;
    std::unordered_map<std::string, std::string> m_metadata;
    std::vector<std::vector<std::string>> m_suggested_plots;
};
