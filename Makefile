# Simple Makefile for building focusZ plugin
# Usage: make or make install

CXX        ?= g++
CXXFLAGS   ?= -O2 -Wall -Wextra
PKG_CONFIG ?= pkg-config

# Required pkg-config packages
PKGS := hyprland pixman-1 libdrm

# Compiler flags
CXXFLAGS += -shared -fPIC -std=c++2b $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDFLAGS  += $(shell $(PKG_CONFIG) --libs $(PKGS))

# Layout
SRCDIR   := src
OBJDIR   := build
TARGET    = libfocusZ.so

SOURCES   = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS   = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

# Hyprland plugin install path. There is no arg-less `hyprctl` command that
# reports this (hyprctl plugins errors out), so use the standard location;
# override with `make install PLUGIN_DIR=...` if needed.
PLUGIN_DIR ?= $(HOME)/.local/share/hyprland/plugins

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(wildcard $(SRCDIR)/*.hpp)
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

install: $(TARGET)
	install -d $(DESTDIR)$(PLUGIN_DIR)
	install -m 755 $(TARGET) $(DESTDIR)$(PLUGIN_DIR)/$(TARGET)
	@echo "Installed to $(DESTDIR)$(PLUGIN_DIR)/$(TARGET)"
	@echo "Enable it in your Hyprland config:"
	@echo "  plugin = /abs/path/to/$(TARGET)"
	@echo "  plugin { focusZ { enabled = true } }"

clean:
	rm -rf $(OBJDIR) $(TARGET)
