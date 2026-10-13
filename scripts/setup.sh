#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ -f .env ]; then
  echo '.env already exists; existing settings preserved.'
  exit 0
fi
umask 077
{
  printf 'DB_PASSWORD=%s\n' "$(openssl rand -hex 24)"
  printf 'MYSQL_ROOT_PASSWORD=%s\n' "$(openssl rand -hex 24)"
  printf 'BILLING_KEY=%s\n' "$(openssl rand -hex 24)"
  printf 'DEMO_PASSWORD=Campus2026!\nAPP_PORT=8080\nMYSQL_HOST_PORT=3307\n'
} > .env
echo 'Local .env created. Demo password: Campus2026!'
