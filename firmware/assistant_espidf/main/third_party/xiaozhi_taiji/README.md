# Taiji Pi S3 panel data provenance

`../../taiji_panel_init.h` retains the two initialization sequences from
`main/boards/taiji-pi-s3/taiji_pi_s3.cc` at XiaoZhi v2.5.2 commit
`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`.

The fixed source tree has no nested license for this board directory. Its
repository MIT notice is retained in `LICENSE`; the project license is unchanged.
The STD sequence has 216 commands and the PDM sequence has 184. Every active
source initializer was consumed in order; data lengths match the literal arrays,
with a maximum of 14 bytes. The source's commented-out entries remain inactive.

The fixed README associates batch numbers greater than 2528 with a changed
microphone and display glass, selecting the PDM variant. These sequences are
separate hardware configuration candidates. Neither is a verified panel probing
algorithm or proof of physical behavior on another batch.
