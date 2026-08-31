binext :=

ifeq ($(platform),win32)
   binext := .exe
   static := 1
endif

ifeq ($(static),1)
   lflags += -static-libgcc -static
endif

cflags += -O2 -g -Wall $(extracflags)
lflags +=
libs   += -lm
bins   += pj64tosrm$(binext) m64pmigrate$(binext)

.PHONY: all clean tools

all: $(bins)
clean:
	-rm -f $(bins)
	-$(MAKE) -C .. clean-tools

# angrylion_bench and angrylion_verify link against the core's angrylion
# objects, so the top-level Makefile builds them; `make tools` here is the
# same as `make tools` in the repository root.
tools:
	$(MAKE) -C .. tools

pj64tosrm$(binext): pj64tosrm.c
	$(CC) $(cflags) -o$@ $(lflags) $< $(libs)

m64pmigrate$(binext): m64pmigrate.c
	$(CC) $(cflags) -o$@ $(lflags) $< $(libs)

%.o: %.c
	$(CC) $(cflags) -c -o $@ $<

