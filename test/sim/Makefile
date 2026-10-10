# Host simulation of the real firmware DSP + display/input code.
# Outputs to sim_out/: dry.wav, wet.wav (audio through the EQ engine),
# monitor.png (the 480x320 control monitor), and the EQ curve to stdout.
CC ?= cc
CFLAGS = -O2 -DHX_SIM -I ../../src -I ../../src/dsp
SRC_DSP = ../../src/dsp/engine.c ../../src/dsp/biquad.c ../../src/dsp/nam_a2.c
SRC_UI  = ../../src/input/input.c ../../src/input/encoder.c ../../src/input/switch.c ../../src/display.c
OUT = ../../sim_out

sim: | $(OUT)
	$(CC) $(CFLAGS) -o $(OUT)/sim sim.c $(SRC_DSP) $(SRC_UI) -lm
	$(CC) $(CFLAGS) -o $(OUT)/eqcurve eqcurve.c $(SRC_DSP) -lm
	cd $(OUT) && ./sim && ./eqcurve
	sips -s format png $(OUT)/monitor.ppm --out $(OUT)/monitor.png >/dev/null
	@echo "sim_out/: dry.wav wet.wav monitor.png"
$(OUT):
	mkdir -p $(OUT)
clean:
	rm -rf $(OUT)
.PHONY: sim clean
