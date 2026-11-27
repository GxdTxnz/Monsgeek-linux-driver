#!/bin/bash
set -u

deb=${1:?path to .deb is required}
image=${2:-debian:13}
docker=${DOCKER:-docker}

if [ ! -f "$deb" ]
then
  echo "FAILED  no such file $deb"
  exit 1
fi

dir=$(cd "$(dirname "$deb")" && pwd)
name=$(basename "$deb")

$docker run --rm -v "$dir:/pkg:ro" "$image" bash -c "
  set -u
  fail=0
  check() { if eval \"\$2\"; then echo \"OK      \$1\"; else echo \"FAILED  \$1\"; fail=1; fi; }

  apt-get update -qq >/dev/null 2>&1
  apt-get install -y -qq systemd >/dev/null 2>&1
  wants=/etc/systemd/user/default.target.wants/monsgeek-bridge.service
  check 'package installs' 'apt-get install -y -qq /pkg/$name >/tmp/install.log 2>&1 || { tail -20 /tmp/install.log; false; }'
  check 'binary in /usr/bin' '[ -x /usr/bin/monsgeek-bridge ]'
  check 'udev rule' 'grep -q \"ATTRS{idVendor}==\\\"3151\\\", ATTRS{idProduct}==\\\"5030\\\"\" /usr/lib/udev/rules.d/70-monsgeek.rules'
  check 'user service' 'grep -q \"^ExecStart=/usr/bin/monsgeek-bridge\$\" /usr/lib/systemd/user/monsgeek-bridge.service'
  check 'service enabled for all users' '[ -L \$wants ]'
  check 'README' '[ -f /usr/share/doc/monsgeek-bridge/README.md ]'
  check 'license' 'grep -q \"GNU GENERAL PUBLIC LICENSE\" /usr/share/doc/monsgeek-bridge/copyright'
  check 'no libprotobuf dependency' '! ldd /usr/bin/monsgeek-bridge | grep -q libprotobuf'
  check 'binary runs' 'monsgeek-bridge --no-such-flag 2>&1 | grep -q \"usage\"'
  check 'package upgrades' 'apt-get install -y -qq --reinstall /pkg/$name >/dev/null 2>&1 && [ -L \$wants ]'
  check 'package removes' 'apt-get remove -y -qq monsgeek-bridge >/dev/null 2>&1 && [ ! -e /usr/bin/monsgeek-bridge ]'
  check 'service disabled after removal' '[ ! -e \$wants ]'
  exit \$fail
"
