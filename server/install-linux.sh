#!/bin/sh
# 리눅스(Debian·Proxmox)에 상시 서비스로 설치. root 로: sh install-linux.sh
set -e
cd "$(dirname "$0")"
install -m 755 hostinfo_serial.py /usr/local/bin/hostinfo_serial.py
install -m 644 hostinfo-serial.service /etc/systemd/system/hostinfo-serial.service
[ -f /etc/default/hostinfo-serial ] || echo "HOSTINFO_ARGS=" > /etc/default/hostinfo-serial
systemctl daemon-reload
systemctl enable hostinfo-serial.service
systemctl restart hostinfo-serial.service   # 이미 돌고 있으면 새 판으로
systemctl --no-pager --lines=3 status hostinfo-serial.service || true
