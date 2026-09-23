# FAQ

Frequently asked questions about Xyce Studio.

## Is Xyce Studio a simulator?

No. Xyce Studio is a graphical environment that drives the **Xyce**
executable: it prepares the netlist, runs Xyce as a child process, and
visualizes its output. All circuit simulation is performed by Xyce.

## Do I need Xyce installed separately?

Yes. Xyce Studio does not bundle a simulator. Install Xyce yourself and
point Xyce Studio to the executable (see
[Configuring the Xyce Executable](user-guide/configuration.md)).

## Does Xyce Studio work without KiCad?

Yes — on macOS. The standalone application opens netlist files (`.cir`)
and simulation output files (`.raw`, `.prn`, `.csv`, `.csd`, `.dat`)
directly, without any KiCad involvement. The KiCad integration is only
one way of launching the application, and it extracts the netlist from
your schematic automatically.

## Which KiCad versions are supported?

KiCad 10.0 or newer, with the KiCad API enabled
(**Preferences** → **Settings** → **API**). See
[Installation](getting-started/installation.md) for the plugin setup
steps.

## Which operating systems are supported?

- The **KiCad plugin** ships for macOS, Windows and Linux
  (`kicad-xyce-plugin-macos.zip`, `kicad-xyce-plugin-windows.zip`,
  `kicad-xyce-plugin-linux.zip`).
- The **standalone application** ships for macOS and Linux
  (`xyce-studio-macos.zip`, `xyce-studio-linux.zip`).
- Minimum versions: **macOS 13.3 Ventura** (Apple silicon and Intel);
  **Windows 10 or newer (64-bit)**; **Ubuntu 24.04 LTS or any distribution
  with glibc 2.39+**, on X11 or Wayland — see
  [Installation](getting-started/installation.md).

## Which Xyce versions are supported?

Xyce Studio drives whatever Xyce executable you configure; there is no
bundled or pinned Xyce version. Features like Harmonic Balance and
Linear analysis depend on the capabilities of your installed Xyce
release.

## What output file formats can the waveform viewer read?

All plottable Xyce `.PRINT` formats: RAW (`.raw`), PROBE (`.csd`),
CSV (`.csv`), TECPLOT (`.dat`) and the `.prn` formats
(STD, NOINDEX, GNUPLOT, SPLOT), plus Touchstone (`.s2p`) files from
`.LIN` analysis. See
[Simulation Output Files](user-guide/output-files.md).

## Where can I download it?

From the
[releases page](https://github.com/spice-projects/xyce-studio/releases/latest).

## How do I report a problem?

Open a [GitHub issue](https://github.com/spice-projects/xyce-studio/issues)
with your platform and versions, the steps to reproduce, and the
contents of the [Simulation Output panel](user-guide/main-window.md#simulation-output-panel)
— see [Troubleshooting](user-guide/troubleshooting.md) first for known
failure modes.
