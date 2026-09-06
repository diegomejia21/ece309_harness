#!/usr/bin/env bash
# Builds github.zip for submission: the whole repository, minus build
# products and the zip itself. Run from the repository root.
set -eu
rm -f github.zip
if command -v git >/dev/null 2>&1 && [ -d .git ]; then
  # Archive exactly what is committed -- guarantees the zip matches the repo.
  git archive --format=zip -o github.zip HEAD
else
  zip -r github.zip . -x '*.git*' 'harness' 'harness_asan' 'github.zip'
fi
echo "wrote github.zip ($(du -h github.zip | cut -f1))"
echo "Remember to put your repository URL in github.txt"
