# Host-side unit tests for the DSP (run on your dev machine, not the device).
CC ?= cc
CFLAGS = -O2 -Wall -I..
all: test
test: test_biquad test_engine test_input
	./test_biquad && ./test_engine && ./test_input
test_biquad: test_biquad.c ../src/dsp/biquad.c
	$(CC) $(CFLAGS) -o $@ $^ -lm
test_engine: test_engine.c ../src/dsp/engine.c ../src/dsp/biquad.c ../src/dsp/nam_a2.c
	$(CC) $(CFLAGS) -o $@ $^ -lm
# NAM A2 block. Needs a model: make test-nam NAM_MODEL=path/to/A2.nam [NAM_CH=3|8]
# With NAM_IN/NAM_REF (raw float32 mono) it is also compared to a NAM Core render.
NAM_CH ?= 3
test-nam: test_nam_a2
	./test_nam_a2 $(NAM_IN) $(NAM_REF)
nam_a2_model.h: $(NAM_MODEL) ../tools/nam2c.py
	python3 ../tools/nam2c.py $(NAM_MODEL) $@ --channels $(NAM_CH)
test_nam_a2: test_nam_a2.c ../src/dsp/nam_a2.c nam_a2_model.h
	$(CC) $(CFLAGS) -I. -o $@ test_nam_a2.c ../src/dsp/nam_a2.c -lm
clean:
	rm -f test_biquad test_engine test_input test_nam_a2 nam_a2_model.h
.PHONY: all test test-nam clean

test_input: test_input.c ../src/input/encoder.c ../src/input/switch.c
	$(CC) $(CFLAGS) -o $@ $^
