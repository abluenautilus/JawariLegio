# Project Name
TARGET = TanpuraLegio

USE_FATFS = 1

# Sources
CPP_SOURCES = Tanpura.cpp
#DEBUG=1

# Library Locations
USE_DAISYSP_LGPL=1
LIBDAISY_DIR = /Users/jonask/Music/Daisy/libDaisy
DAISYSP_DIR = /Users/jonask/Music/Daisy/DaisySP

# Linker flags
# This is not really required, used only for profiling! Increases executable size by ~8kB
LDFLAGS = -u _printf_float

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

