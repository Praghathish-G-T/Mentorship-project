#!/bin/bash
echo "Compiling backend server..."
gcc Backend/main.c Backend/api_handler.c Backend/mentorship_data.c Backend/json_helpers.c \
    -o mentor_backend \
    -lmicrohttpd -lcjson -std=c11 -Wall -Wextra -g

if [ $? -eq 0 ]; then
  echo "Compilation successful! Executable: mentor_backend"
else
  echo "Compilation failed."
fi
