# The `.PLOT` Directive

Without a `.PLOT` directive, every run opens the charts panel with a single
empty chart and you pick the expressions yourself. A `.PLOT` line declares
which charts the run should produce:

```spice
.PLOT V(N1) V(N2,N3) abs(I(R1))
.PLOT I(R3)
```

The first line creates a chart with three series, the second one a second
chart with a single series.

!!! note "This is an Xyce Studio extension"
    `.PLOT` is **not** part of the Xyce netlist language — the Xyce Reference
    Guide lists it as unsupported (Table 6-1: *".VECTOR, .WATCH, and .PLOT
    … Xyce does not support these commands"*). Xyce Studio interprets the
    directive itself and never hands it to the simulator, so the simulation log
    stays clean. The same netlist still runs under plain Xyce; there it
    produces one *“Unrecognized dot line will be ignored”* warning per `.PLOT`
    line and the simulation completes normally.

In [KiCad](https://www.kicad.org/), add the directive as schematic text or in a
text box — its SPICE exporter passes `.PLOT` through like any other directive.

## Syntax

```spice
.PLOT <expression> [<expression> ...]
```

- **One `.PLOT` line = one chart**, and every expression on it becomes one
  series of that chart. Add more `.PLOT` lines for more charts.
- **Expressions are separated exactly as in a `.PRINT` line**: by whitespace.
  Commas between the expressions are accepted too, so
  `.PLOT V(N1), V(N2)` and `.PLOT V(N1) V(N2)` are the same. A comma inside an
  argument list (`V(N2,N3)`) or a braced expression (`{V(1), V(2)}`) belongs
  to the expression.
- **Wrap expressions containing spaces in braces**, the same way a `.PRINT`
  line does: `.PLOT {V(1) * 2}`. The braces are removed for the chart.
- **Any expression the charts panel understands works**: solution variables
  (`V(out)`, `V(a,b)`, `I(R1)`, `IC(Q1)`, `P(R1)`, `R1:res`), functions
  (`abs`, `db`, `mag`, `phase`, …), arithmetic and conditionals. This is
  exactly the syntax of the **Custom expressions** field of the
  [Select Plot Expressions dialog](charts.md#adding-and-removing-plots).
- **A leading analysis type is ignored**, so HSPICE and PSpice lines import
  unchanged: `.PLOT TRAN V(1) V(2)` plots `V(1)` and `V(2)` when the run is a
  transient analysis.
- `W(...)` is accepted as the PSpice alias of `P(...)` and normalized to
  `P(...)`.

## The `.PLOT` quantities are printed for you

A chart can only show what the run produced, so the quantities your
expressions read are added to the analysis `.PRINT` automatically:

```spice
.TRAN 1u 20m
.PRINT TRAN FORMAT=RAW V(N1)
.PLOT abs(I(R1))
```

The `.PRINT` handed to Xyce becomes
`.PRINT TRAN FORMAT=RAW V(N1) I(R1)` — only the missing quantity is added,
the format and your own variables are untouched, and a printed wildcard
(`V(*)`) is never duplicated. When the netlist has no `.PRINT` at all, one is
created for the analysis from the plotted quantities, so `.PLOT` alone is
enough (a `.LIN` run is the exception: it writes a touchstone file instead of
an analysis print). The merged `.PRINT` is what you see in the editor and in
the [simulation parameters dialog](simulations.md) — nothing happens behind
your back.

Only quantities of the analysis print are added. A per-step selector such as
`I(R1)@2` or a network parameter such as `S11` reads no printable quantity, so
it contributes nothing. An expression the viewer cannot evaluate — a netlist
`.FUNC`, for example — still gets its nested quantities printed, but its
series is skipped when the charts are built.

## Re-running with edited directives

- The declared charts are applied when a result dataset is opened for the
  first time.
- Re-running the **same** netlist keeps the charts you arranged — including
  zoom windows and selected steps.
- Re-running a netlist whose `.PLOT` lines **changed** rebuilds the charts of
  the primary tab from the new directives. Manual changes to those charts are
  replaced, because the directives are the source of truth.
- Removing every `.PLOT` line and re-running keeps the existing charts;
  closing the tab or opening another file starts from an empty chart again.

## Comparison with HSPICE and PSpice

In HSPICE and PSpice, `.PLOT` asks the simulator to draw waveforms on the
line printer. Xyce Studio uses the otherwise unused name for something else:
it groups the charts the results open with. The two meanings never mix —
Xyce Studio reads the line as a chart declaration and never sends it to the
simulator.

## Troubleshooting

- **A series is missing from the chart** — the expression could not be
  resolved from the output. Check the log panel and make sure the quantity it
  reads is valid for the analysis (a `.TRAN` run cannot print `S11`, an AC
  print cannot print `P(R1)`).
- **The charts do not follow the schematic** — `.PLOT` lines only reach the
  netlist through a directive-capable text item; check the text in the
  schematic is not marked *Exclude from simulation*.
- **The directive is gone after saving** — it is never removed; like the other
  directives Xyce Studio interprets, it is re-emitted in the block above
  `.END` of the editor netlist and saved with the file. Only the copy handed
  to the simulator leaves it out.
