#pragma once

#include <string>
#include <vector>

class XyceOutputFile;

namespace fourier
{
    // one harmonic row of a Fourier results table, mirroring the columns of the .FOUR file
    struct ResultRow
    {
        // the harmonic index the file prints, where zero names the dc component
        int harmonic = 0;

        // the harmonic frequency, formatted as the file writes it
        std::string frequency;

        // the magnitude of the complex spectrum, formatted as the file writes it
        std::string magnitude;

        // the phase of the complex spectrum in degrees, formatted as the file writes it
        std::string phase;

        // the magnitude relative to the fundamental, which the file prints as `Norm. Mag`
        std::string norm_magnitude = "";

        // the phase relative to the fundamental, which the file prints as `Norm. Phase`
        std::string norm_phase = "";
    };

    // one signal and step block: its header, its analysis settings and its harmonic rows
    struct SignalTable
    {
        // the signal the block belongs to
        std::string signal;

        // the step label, empty for a single-step file
        std::string step = "";

        // the total harmonic distortion the block records
        std::string thd = "";

        // the harmonic count the analysis reported, empty when the file records none
        std::string harmonics = "";

        // the interpolation grid points the analysis used, empty when the file records none
        std::string gridsize = "";

        // the interpolation type the analysis used, empty when the file records none
        std::string interpolation = "";

        // the harmonic rows of the block
        std::vector<ResultRow> rows;
    };

    // build the results tables of a Fourier output file, one block per signal and step
    [[nodiscard]] std::vector<SignalTable> build_tables(XyceOutputFile& file);
} // namespace fourier
