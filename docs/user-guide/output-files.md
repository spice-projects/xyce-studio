# Simulation Output Files

Every analysis in Xyce writes one or more output files next to the netlist.
Which files appear depends on the analysis type and the `FORMAT` option of the
`.PRINT` directive. This page lists every trigger from the Xyce Reference
Guide (§2.1.31, print output tables 2-19 to 2-29) with the produced file,
which of the files is the primary one, and whether Xyce Studio can load it.

## Notation

- `circuit` is the netlist file name without its extension
- **Primary** files receive the user-specified `FILE=` name; secondary files
  always use the default netlist-derived name
- Xyce Studio parsers: **raw** (`xyce_raw_file`), **prn**
  (`xyce_prn_file`, STD/NOINDEX/GNUPLOT/SPLOT formats), **csd**
  (`xyce_csd_file`, PROBE format) and **touchstone** (`.LIN` s-parameter
  output)

## AC analysis

AC generates two files: the primary frequency-domain file and the
secondary time-domain initial-conditions file (enabled by adding `.OP`).

| Trigger | File | Domain | P/S | Studio parser |
| --- | --- | --- | --- | --- |
| `.PRINT AC`, GNUPLOT, SPLOT, NOINDEX | `circuit.FD.prn` | freq | P | prn |
| `.PRINT AC FORMAT=CSV` | `circuit.FD.csv` | freq | P | — |
| `.PRINT AC FORMAT=RAW` (incl. `Xyce -a`) | `circuit.raw` | freq | P | raw |
| `.PRINT AC FORMAT=TECPLOT` | `circuit.FD.dat` | freq | P | — |
| `.PRINT AC FORMAT=PROBE` | `circuit.csd` | freq | P | csd |
| `.PRINT AC_IC` | `circuit.TD.prn` | time | S | prn |
| `.PRINT AC_IC FORMAT=CSV` | `circuit.TD.csv` | time | S | — |
| `.PRINT AC_IC FORMAT=RAW` | `circuit.raw` | time | S | raw |
| `.PRINT AC_IC FORMAT=TECPLOT` | `circuit.TD.dat` | time | S | — |
| `.PRINT AC_IC FORMAT=PROBE` | `circuit.TD.csd` | time | S | csd |

## DC analysis

| Trigger | File | Studio parser |
| --- | --- | --- |
| `.PRINT DC`, GNUPLOT, SPLOT, NOINDEX | `circuit.prn` | prn |
| `.PRINT DC FORMAT=CSV` | `circuit.csv` | — |
| `.PRINT DC FORMAT=RAW` (incl. `Xyce -a`) | `circuit.raw` | raw |
| `.PRINT DC FORMAT=TECPLOT` | `circuit.dat` | — |
| `.PRINT DC FORMAT=PROBE` | `circuit.csd` | csd |

## Harmonic balance (HB)

`.PRINT HB` produces all three files at once; a `FILE=` name only redirects
the primary (time-domain) file. With `.STEP` the IC data goes through a
temporary file before being appended to `circuit.hb_ic.prn`.

| Trigger | File | Domain | P/S | Studio parser |
| --- | --- | --- | --- | --- |
| `.PRINT HB`, GNUPLOT, SPLOT | `circuit.HB.TD.prn` | time | P | prn |
| | `circuit.HB.FD.prn` | freq | S | prn |
| | `circuit.hb_ic.prn` | time | S | prn |
| `.PRINT HB FORMAT=CSV` | `circuit.HB.TD.csv`, `circuit.HB.FD.csv`, `circuit.hb_ic.csv` | — | P, S, S | — |
| `.PRINT HB FORMAT=TECPLOT` | `circuit.HB.TD.dat`, `circuit.HB.FD.dat`, `circuit.hb_ic.dat` | — | P, S, S | — |
| `.PRINT HB_FD` | `circuit.HB.FD.prn` (also `.csv`, `.dat`) | freq | P | prn |
| `.PRINT HB_TD` | `circuit.HB.TD.prn` (also `.csv`, `.dat`) | time | P | prn |
| `.PRINT HB_STARTUP` (with `HBINT STARTUPPERIODS`) | `circuit.startup.prn` (also `.csv`, `.dat`) | time | S | prn |
| `.PRINT HB_IC` (with `SAVEICDATA=1`) | `circuit.hb_ic.prn` (also `.csv`, `.dat`) | time | S | prn |

HB supports neither RAW nor PROBE format.

## Noise analysis

| Trigger | File | Studio parser |
| --- | --- | --- |
| `.PRINT NOISE`, GNUPLOT, SPLOT, NOINDEX | `circuit.NOISE.prn` | prn |
| `.PRINT NOISE FORMAT=CSV` | `circuit.NOISE.csv` | — |
| `.PRINT NOISE FORMAT=TECPLOT` | `circuit.NOISE.dat` | — |

Noise supports neither RAW nor PROBE format.

## Transient analysis

| Trigger | File | Studio parser |
| --- | --- | --- |
| `.PRINT TRAN`, GNUPLOT, SPLOT, NOINDEX | `circuit.prn` | prn |
| `.PRINT TRAN FORMAT=CSV` | `circuit.csv` | — |
| `.PRINT TRAN FORMAT=RAW` (incl. `Xyce -a`) | `circuit.raw` | raw |
| `.PRINT TRAN FORMAT=TECPLOT` | `circuit.dat` | — |
| `.PRINT TRAN FORMAT=PROBE` | `circuit.csd` | csd |

## Homotopy analysis

Requires `.OPTIONS NONLIN CONTINUATION=<method>`.

| Trigger | File | Studio parser |
| --- | --- | --- |
| `.PRINT HOMOTOPY`, GNUPLOT, SPLOT, NOINDEX | `circuit.HOMOTOPY.prn` | prn |
| `.PRINT HOMOTOPY FORMAT=CSV` | `circuit.HOMOTOPY.csv` | — |
| `.PRINT HOMOTOPY FORMAT=TECPLOT` | `circuit.HOMOTOPY.dat` | — |

PROBE and RAW are unsupported for homotopy; Xyce falls back to the STD
format with a warning.

## Sensitivity analysis

Requires `.SENS`; transient adjoint sensitivity uses its own print line.

| Trigger | File | Studio parser |
| --- | --- | --- |
| `.PRINT SENS` (TRAN/DC) | `circuit.SENS.prn`, `.csv`, `.dat` | — |
| `.PRINT SENS` (AC) | `circuit.FD.SENS.prn`, `.csv`, `.dat` | — |
| `.PRINT TRANADJOINT` (+NOINDEX/CSV/TECPLOT) | `circuit.TRADJ.prn`, `.csv`, `.dat` | — |

## Embedded sampling analysis

| Trigger | File | Studio parser |
| --- | --- | --- |
| `.PRINT ES`, GNUPLOT, SPLOT, NOINDEX | `circuit.ES.prn` | prn |
| `.PRINT ES FORMAT=CSV` | `circuit.ES.csv` | — |
| `.PRINT ES FORMAT=TECPLOT` | `circuit.ES.dat` | — |

## Intrusive PCE analysis

| Trigger | File | Studio parser |
| --- | --- | --- |
| `.PRINT PCE`, GNUPLOT, SPLOT, NOINDEX | `circuit.PCE.prn` | prn |
| `.PRINT PCE FORMAT=CSV` | `circuit.PCE.csv` | — |
| `.PRINT PCE FORMAT=TECPLOT` | `circuit.PCE.dat` | — |

## Summary of format support

| Format | Extension | Supported analyses | Studio parser |
| --- | --- | --- | --- |
| STD (default), NOINDEX, GNUPLOT, SPLOT | `.prn` | all | prn |
| CSV | `.csv` | all | — |
| TECPLOT | `.dat` | all | — |
| RAW (incl. `Xyce -a`) | `.raw` | AC, AC_IC, DC, TRAN | raw |
| PROBE | `.csd` / `.TD.csd` | AC, AC_IC, DC, TRAN | csd |

## Additional outputs

- `Xyce -r <name> [-a]` — raw override file with all circuit variables,
  regardless of the `.PRINT` variable lists; parsed by the raw parser
- `.LIN` — Touchstone s-parameter output (`.s2p`); parsed by the
  touchstone parser
- `.OP` — operating point data in the log file, not a plottable file
