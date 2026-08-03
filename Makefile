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

# Hyprland plugin install path
PLUGIN_DIR = $(shell hyprctl plugins 2>/dev/null | head -1 | sed 's/.*: //')
ifeq ($(strip $(PLUGIN_DIR)),)
PLUGIN_DIR = $(HOME)/.local/share/hyprland/plugins
endif

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
