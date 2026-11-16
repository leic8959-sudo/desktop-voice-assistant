SSD2683 OTP and old/new pixel transition handling in ../../paper_display.cpp derives from Zectrix Lab's MIT reference component.

Repository: https://github.com/itopinion/zectrix-note4-epd-demo
Commit: ca285c98ed0641f86780edb1f5ec77b0335fe649
Source: components/zectrix_epd/zectrix_epd.cc

Only the black/white protocol is adapted. Shared bounded refresh worker, DMA lifetime and LVGL handoff remain in the existing local implementation. Native IDF three-wire half-duplex transfers replace bus destruction/recreation for temperature reads. No grayscale waveform table or demo application is linked.
