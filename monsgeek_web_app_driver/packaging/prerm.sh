#!/bin/sh
set -e

if [ "$1" != "remove" ] || ! command -v systemctl >/dev/null 2>&1
then
  exit 0
fi

for dir in /run/user/*
do
  [ -S "$dir/systemd/private" ] || continue
  user=$(id -nu "${dir##*/}" 2>/dev/null) || continue
  systemctl --user -M "$user@" stop monsgeek-bridge.service || true
done

systemctl --global disable monsgeek-bridge.service || true
