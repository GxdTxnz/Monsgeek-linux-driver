#!/bin/sh
set -e

if command -v udevadm >/dev/null 2>&1
then
  udevadm control --reload-rules || true
  udevadm trigger --subsystem-match=hidraw --action=change || true
fi

if command -v systemctl >/dev/null 2>&1
then
  for dir in /run/user/*
  do
    [ -S "$dir/systemd/private" ] || continue
    user=$(id -nu "${dir##*/}" 2>/dev/null) || continue
    systemctl --user -M "$user@" daemon-reload || true
  done
fi
