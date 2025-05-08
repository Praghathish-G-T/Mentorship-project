# Simple Makefile for Mentor Dashboard Backend

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -I/usr/include/ # Basic flags, add include paths if needed (e.g., for cJSON)
# Add -I/path/to/cjson/include if cJSON headers are not in standard paths
# Add -I/path/to/microhttpd/include if MHD headers are not in standard paths

# Libraries to link
# Add -L/path/to/cjson/lib and -L/path/to/microhttpd/lib if libs are not in standard paths
LIBS = -lmicrohttpd -lcjson

# Source files
SRCS = main.c api_handler.c mentorship_data.c json_helpers.c

# Object files (derived from source files)
OBJS = $(SRCS:.c=.o)

# Executable name
TARGET = mentor_backend

# Default target
all: $(TARGET)

# Link object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)
	@echo "Backend executable '$(TARGET)' created."

# Compile source files into object files
%.o: %.c mentorship_data.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up object files and the executable
clean:
	rm -f $(OBJS) $(TARGET) mentorship_data.json # Also remove data file on clean
	@echo "Cleaned up build files."

# Phony targets (targets that aren't actual files)
.PHONY: all clean

