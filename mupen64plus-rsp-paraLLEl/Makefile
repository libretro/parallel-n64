TARGET := main
ARCH := x86_64

SOURCES := $(wildcard *.cpp) $(wildcard rsp/*.cpp) $(wildcard arch/$(ARCH)/rsp/*.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
DEPS := $(OBJECTS:.o=.d)
CXXFLAGS += -Iarch/$(ARCH)/rsp

CXXFLAGS += -Wall
ifeq ($(DEBUG), 1)
	CXXFLAGS += -O0 -g
else
	CXXFLAGS += -Ofast -march=native -g
endif

ifeq ($(DEBUG_JIT), 1)
	CXXFLAGS += -DDEBUG_JIT
	LDFLAGS += -rdynamic
endif

LDFLAGS += -lclangFrontend \
			  -lclangSerialization \
			  -lclangDriver \
			  -lclangCodeGen \
			  -lclangParse \
			  -lclangSema \
			  -lclangStaticAnalyzerFrontend \
			  -lclangStaticAnalyzerCheckers \
			  -lclangStaticAnalyzerCore \
			  -lclangAnalysis \
			  -lclangRewriteFrontend \
			  -lclangRewrite \
			  -lclangEdit \
			  -lclangAST \
			  -lclangLex \
			  -lclangBasic
LDFLAGS += $(shell llvm-config --ldflags --libs --system-libs)

all: $(TARGET)

-include $(DEPS)

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS) -MMD

clean:
	$(RM) -f $(OBJECTS) $(TARGET) $(DEPS)

.PHONY: clean
