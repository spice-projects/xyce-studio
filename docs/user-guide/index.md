# Xyce Studio User Guide

Xyce Studio is a desktop application for simulating electronic circuits with
the Xyce circuit simulator. You describe your circuit in a SPICE-style
netlist, choose the analysis you want, run the simulation, and inspect the
results as interactive plots — all from a single window.

Xyce Studio can be used in two ways:

- **As a KiCad plugin** — launched directly from the KiCad schematic editor.
  The netlist is extracted from your schematic automatically, so you can go
  from schematic to simulation results without leaving KiCad.
- **As a standalone application** — open Xyce netlist files (`.cir`) or
  simulation output files (`.raw`, `.prn`, `.csv`, `.csd`, `.dat`) directly,
  edit them, and run simulations on your own schedule.

Both modes share the same window layout, dialogs, and plotting tools, so
everything in this guide applies to both unless explicitly noted.

## Contents

1. [Installation](installation.md)
2. [The Main Window](main-window.md)
3. [Configuring the Xyce Executable](configuration.md)
4. [Running Simulations](simulations.md)
   - [Operating Point (.OP)](simulations.md#operating-point-op)
   - [Transient (.TRAN)](simulations.md#transient-tran)
   - [DC Sweep (.DC)](simulations.md#dc-sweep-dc)
   - [AC Analysis (.AC)](simulations.md#ac-analysis-ac)
   - [Noise (.NOISE)](simulations.md#noise-noise)
   - [Harmonic Balance (.HB)](simulations.md#harmonic-balance-hb)
   - [Linear Analysis (.LIN)](simulations.md#linear-analysis-lin)
   - [Directives: .PRINT, .FFT, .FOUR, .MEASURE](simulations.md#print-output-options)
5. [Simulation Output Files](output-files.md)
6. [Viewing Results](charts.md)
7. [Troubleshooting](troubleshooting.md)

## A typical workflow

1. Launch Xyce Studio from the KiCad schematic editor (or open a netlist
   file in the standalone app).
2. Review the netlist in the built-in editor.
3. Click **Configure** in the toolbar and set up the analysis parameters.
4. Click **Run Simulation** and watch the status bar.
5. Switch to the charts view, pick the expressions you want to plot, and
   analyze the waveforms — zoom, hover readouts, FFT, and parametric step
   filtering are all available.

![Xyce Studio main window](images/main-window.png)

## Units and numbers

All numeric fields accept standard SPICE engineering suffixes: `1n`
(nano), `2k` (kilo), `25m` (milli), `5u` (micro), `10MEG` (mega), `1G`
(giga), and so on. This matches the notation used in netlists, so values
can be copied between the dialogs and the editor verbatim.
