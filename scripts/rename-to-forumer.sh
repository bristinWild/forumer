#!/usr/bin/env bash
# One-off: rename the sample module example_forum -> forumer.
set -euo pipefail
cd "$(dirname "$0")/.."

# 1. Rename source files (git mv keeps history)
git mv src/example_forum.rep          src/forumer.rep
git mv src/example_forum_backend.h    src/forumer_backend.h
git mv src/example_forum_backend.cpp  src/forumer_backend.cpp

# 2. Replace every spelling in the module's own files
FILES="metadata.json CMakeLists.txt flake.nix src/forumer.rep src/forumer_backend.h src/forumer_backend.cpp src/forum_message.h src/qml/Main.qml"
perl -pi -e '
  s/EXAMPLE_FORUM/FORUMER/g;
  s/ExampleForum/Forumer/g;
  s/example_forum/forumer/g;
  s/example-forum/forumer/g;
  s/Example Forum/Forumer/g;
' $FILES

# 3. Metadata: version + description
perl -pi -e '
  s/"version": "[^"]*"/"version": "0.1.0"/;
  s/"description": "[^"]*"/"description": "Forumer: a private, serverless forum on the Logos stack. Personas, identity rotation, offline catch-up and an outbox."/;
' metadata.json

echo "Remaining references (should be none):"
grep -rnI -e example_forum -e ExampleForum -e example-forum -e EXAMPLE_FORUM $FILES || echo "  none"