#!/bin/sh
# Creates and destroys ONLY the disposable course-registration-tests project.
set -eu
cd "$(dirname "$0")/.."
sh scripts/setup.sh
export APP_PORT=18080
mkdir -p output
cleanup() { docker compose -p course-registration-tests down -v > /dev/null 2>&1; }
trap cleanup EXIT INT TERM
# The following is a dedicated temporary test database; never use the demo project name here.
docker compose -p course-registration-tests down -v > /dev/null 2>&1 || true
docker compose -p course-registration-tests up -d --build --wait --wait-timeout 240
if ! python3 tests/integration.py > output/integration-results.txt; then
  docker compose -p course-registration-tests logs --tail=80 app db > output/test-services.log
  cat output/integration-results.txt
  exit 1
fi
cat output/integration-results.txt
