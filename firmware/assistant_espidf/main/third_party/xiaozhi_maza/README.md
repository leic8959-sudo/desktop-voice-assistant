# Maza A01 2.9 panel data provenance

`../../maza_panel_commands.h` retains setup command values and the C221 25C
full/partial waveforms from XiaoZhi v2.5.2 commit
`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`:

- `main/display/epd_display/epd_display.cc`
- `main/display/epd_display/epd_display_2_9.cc`

The fixed source tree has no nested license for these files; its repository MIT
notice is retained verbatim in `LICENSE`. The original base header describes
the display as “by EPDiy”; no independent provenance beyond that fixed source
claim has been established. This attribution does not change the project's
overall license or assert compatibility with other panels/temperatures.

`paper_display.cpp` implements the native worker, bounded SPI/BUSY handling,
latest-frame staging and landscape-to-portrait mapping separately. It preserves
the owning command order and establishes a full base before partial mode; it
transfers exactly 4,736 panel bytes instead of the source's 5,000-byte allocation
length. Panel behavior and waveform/temperature suitability still need hardware
acceptance. No upstream application, display class or UI is vendored wholesale.
