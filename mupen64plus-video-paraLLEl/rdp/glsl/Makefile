OUTPUT := \
	rdp.8.noz.spv \
	rdp.16.noz.spv \
	rdp.32.noz.spv \
	rdp.8.z.spv \
	rdp.16.z.spv \
	rdp.32.z.spv \
	rdp.color.depth.alias.16.spv \
	blit.8.spv \
	blit.16.spv \
	blit.32.spv \
	tmem.rgba32.spv \
	tmem.rgba16.spv \
	tmem.i8.spv \
	tmem.ia8.spv \
	tmem.ia16.spv \
	varying.spv \
	texture.spv \
	combiner.spv

OUTPUT_INC := $(OUTPUT:.spv=.inc)
DEPS := $(OUTPUT:.spv=.inc.d)
GLSLFLAGS := -MD -mfmt=c

all: $(OUTPUT_INC)

-include $(DEPS)

rdp.8.z.inc: rdp.comp
	glslc -o $@ $< -DZ_BUFFER=1 -DCOLOR_BPP=8 -DCOLOR_DEPTH_ALIAS=0 $(GLSLFLAGS)

rdp.16.z.inc: rdp.comp
	glslc -o $@ $< -DZ_BUFFER=1 -DCOLOR_BPP=16 -DCOLOR_DEPTH_ALIAS=0 $(GLSLFLAGS)

rdp.32.z.inc: rdp.comp
	glslc -o $@ $< -DZ_BUFFER=1 -DCOLOR_BPP=32 -DCOLOR_DEPTH_ALIAS=0 $(GLSLFLAGS)

rdp.8.noz.inc: rdp.comp
	glslc -o $@ $< -DZ_BUFFER=0 -DCOLOR_BPP=8 -DCOLOR_DEPTH_ALIAS=0 $(GLSLFLAGS)

rdp.16.noz.inc: rdp.comp
	glslc -o $@ $< -DZ_BUFFER=0 -DCOLOR_BPP=16 -DCOLOR_DEPTH_ALIAS=0 $(GLSLFLAGS)

rdp.32.noz.inc: rdp.comp
	glslc -o $@ $< -DZ_BUFFER=0 -DCOLOR_BPP=32 -DCOLOR_DEPTH_ALIAS=0 $(GLSLFLAGS)

rdp.color.depth.alias.16.inc: rdp.comp
	glslc -o $@ $< -DZ_BUFFER=1 -DCOLOR_BPP=16 -DCOLOR_DEPTH_ALIAS=1 $(GLSLFLAGS)

blit.8.inc: blit.comp
	glslc -o $@ $< -DCOLOR_BPP=8 $(GLSLFLAGS)

blit.16.inc: blit.comp
	glslc -o $@ $< -DCOLOR_BPP=16 $(GLSLFLAGS)

blit.32.inc: blit.comp
	glslc -o $@ $< -DCOLOR_BPP=32 $(GLSLFLAGS)

tmem.rgba32.inc: tmem.comp
	glslc -o $@ $< -DTRANSFER=0 $(GLSLFLAGS)

tmem.rgba16.inc: tmem.comp
	glslc -o $@ $< -DTRANSFER=1 $(GLSLFLAGS)

tmem.i8.inc: tmem.comp
	glslc -o $@ $< -DTRANSFER=2 $(GLSLFLAGS)

tmem.ia8.inc: tmem.comp
	glslc -o $@ $< -DTRANSFER=3 $(GLSLFLAGS)

tmem.ia16.inc: tmem.comp
	glslc -o $@ $< -DTRANSFER=4 $(GLSLFLAGS)

%.inc: %.comp
	glslc -o $@ $< $(GLSLFLAGS)

clean:
	rm -f $(OUTPUT_INC) $(OUTPUT) $(DEPS)

.PHONY: clean
