#!/bin/bash

# Change this to the path to your python annotator script
PYTHON_ANNOTATOR="${ROOT_DIR}/scripts/annotation.py"
JSON_FILE="compile_commands.json"

OUTFILE="$1"
if [ -z "$OUTFILE" ]; then
    echo "Usage: $0 <output-log-file> [directory]"
    exit 1
fi

ROOT="${2:-.}"

find "$ROOT" -type f -name '*.c' | while read -r cfile; do
    echo "Annotating $cfile ..."
    python3 "$PYTHON_ANNOTATOR" "$cfile" "$JSON_FILE" "$OUTFILE"
done
