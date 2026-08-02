# Simple Makefile for building focusZ plugin
# Usage: make or make install

CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra
PKG_CONFIG ?= pkg-config

# Required pkg-config packages
PKGS := hyprland pixman-1 libdrm

# Compiler flags
CXXFLAGS += -shared -fPIC -std=c++2b $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDFLAGS  += $(shell $(PKG_CONFIG) --libs $(PKGS))

# Output
TARGET    = libfocusZ.so
SOURCES   = $(wildcard *.cpp)
OBJECTS   = $(SOURCES:.cpp=.o)

# Hyprland plugin install path
PLUGIN_DIR = $(shell hyprctl plugins 2>/dev/null | head -1 | sed 's/.*: //') 
ifeq ($(strip $(PLUGIN_DIR)),)
PLUGIN_DIR = $(HOME)/.local/share/hyprland/plugins
endif

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

install: $(TARGET)
	install -d $(DESTDIR)$(PLUGIN_DIR)
	install -m 755 $(TARGET) $(DESTDIR)$(PLUGIN_DIR)/$(TARGET)
	@echo "Installed to $(DESTDIR)$(PLUGIN_DIR)/$(TARGET)"
	@echo "Add to your hyprland.conf:"
	@echo "  plugin {"
	@echo "    focusZ {"
	@echo "      enabled = true"
	@echo "    }"
	@echo "  }"

clean:
	rm -f $(OBJECTS) $(TARGET)
