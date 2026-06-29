.PHONY: all clean

CXX=g++
SDIR=src
ODIR=obj

SRCS=$(wildcard $(SDIR)/*.cpp)
OBJS=$(patsubst $(SDIR)/%.cpp, $(ODIR)/%.o, $(SRCS))
DEPS=$(OBJS:%.o=%.d)

WARNINGS=-Wall -Wno-unknown-pragmas -Wno-unused-parameter
CXXFLAGS=-std=c++17 -m32 $(WARNINGS) -I$(SDIR) -fPIC -D_GNU_SOURCE
LDFLAGS=-m32 -shared -ldl

all: fovmod.so

clean:
	rm -rf $(ODIR) fovmod.so

-include $(DEPS)

fovmod.so: $(OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@

$(ODIR)/%.o: $(SDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@
