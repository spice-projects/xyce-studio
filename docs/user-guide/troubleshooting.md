# Troubleshooting

## Simulation does not start

- **“Configured Xyce executable path is invalid”** in the status bar —
  open **Plugin Settings** and make sure the Xyce path points to the
  Xyce executable, not a folder or an unrelated file.
- **“No netlist content to simulate”** — the netlist is empty. In
  standalone mode, open a `.cir` file first; in KiCad mode, check the
  schematic produces a netlist.
- **“Failed to create temporary netlist file”** — a transient system
  problem; retry the run.

## Simulation fails

- Check the [Simulation Output panel](main-window.md#simulation-output-panel)
  for the Xyce error messages — parse errors, unknown devices, and
  convergence failures are reported there line by line.
- **“Simulation failed (exit code N)”** — the Xyce process exited with
  an error; the log above it usually contains the cause.
- **“Simulation finished but output file could not be found”** — the
  run completed but produced no plottable output. Make sure a `.PRINT`
  line is present for the analysis you ran; any plottable format works
  (`RAW`, `PROBE`, `CSV` or the default `.prn` formats — see
  [Simulation Output Files](output-files.md)).
- DC convergence problems: add `.NODESET` hints or `.IC` conditions in
  the [Operating Point tab](simulations.md#operating-point-op), or save
  a converged bias point with `.SAVE` and reuse it.

## FFT does not produce a result

- **“No expressions selected for FFT”** — select at least one
  expression in the FFT dialog.
- **“FFT computation skipped: no data in the selected range”** — the
  chosen Data Range (Current Zoom / Custom) contains no samples. Widen
  the zoom window or use *All*.

## The plugin does not run from KiCad

- Verify the KiCad API is enabled (**Preferences** → **Settings** →
  **API**) and KiCad was restarted afterwards.
- Verify the plugin is listed and enabled in KiCad's **Plugin Manager**.
- If the window opens but the schematic netlist is missing, reopen the
  schematic project and launch the plugin again.

## The UI does not render

- Verify your platform graphics backend (Metal on macOS, Direct3D 11 on
  Windows, OpenGL on Linux) is available and up to date.
