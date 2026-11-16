# Spotpear panel tuning provenance

`../../spotpear_panel_tuning.h` retains panel command data from XiaoZhi v2.5.2,
commit `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`:

- `main/boards/spotpear/sp-esp32-s3-1.28-box/sp-esp32-s3-1.28-box.cc`
- `main/boards/spotpear/sp-esp32-s3-1.54-muma/sp-esp32-s3-1.54-muma.cc`

The fixed tree contains no nested license for these board directories. The
repository MIT notice is retained verbatim in `LICENSE`. This attribution is
limited to these data; it does not change the project's overall license.

BOX's original direct MADCTL write `0x36:0x48` is expressed using the GC9A01
driver's BGR order plus X mirror, preserving the driver's cached rotation.
Its `0x62`, `0x63`, `0xC3`, `0xC4` and MUMA's `0xBB` writes retain their original
values and propagate errors. No upstream touch application, battery estimator
or power-saving application code is copied.
