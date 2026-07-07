PLUGIN_NAME := winctl
SRC         := main.cpp
OUT         := $(PLUGIN_NAME).so

CXX      := g++
# Match whatever C++ standard your installed Hyprland was built with
# (check Hyprland's own meson.build if this fails to compile -- it has
# moved from c++2b to c++23/c++26 across releases).
CXXSTD   := c++23

CXXFLAGS := -shared -fPIC -std=$(CXXSTD) -O2 -Wall -Wextra \
            $(shell pkg-config --cflags hyprland pixman-1 libdrm)
LDFLAGS  := $(shell pkg-config --libs hyprland pixman-1 libdrm)

all: $(OUT)

$(OUT): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

clean:
	rm -f $(OUT)

.PHONY: all clean
