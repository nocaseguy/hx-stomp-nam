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
# The pedal's UI (src/ui) drawn on the host: sim_out/pedal/*.png, three times
# the screen's size. SCRIPT=file runs that in place of the built-in walk.
UI = ../../src/panel.h ../../src/ui/fb.h ../../src/ui/font.h ../../src/ui/draw.h ../../src/ui/scene.h ../../src/ui/pages.h
pedal: pedal_sim.c $(UI) | $(OUT)
	$(CC) -O2 -Wall -Wextra -o $(OUT)/pedal_sim pedal_sim.c
	rm -rf $(OUT)/pedal && mkdir -p $(OUT)/pedal
	$(OUT)/pedal_sim $(OUT)/pedal $(SCRIPT)
	cd $(OUT)/pedal && for f in *.ppm; do sips -s format png -z 720 960 $$f --out $${f%.ppm}.png >/dev/null && rm $$f; done
$(OUT):
	mkdir -p $(OUT)
clean:
	rm -rf $(OUT)
.PHONY: sim pedal clean
