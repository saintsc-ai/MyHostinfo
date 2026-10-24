#!/bin/sh
# 리눅스(Debian·Proxmox)에 상시 서비스로 설치. root 로: sh install-linux.sh
set -e
cd "$(dirname "$0")"
# 서비스가 /usr/bin/python3 로 돌리므로 그것을 확인한다(없거나 3.7 미만이면 설치하지 않음).
PY=/usr/bin/python3
if [ ! -x "$PY" ]; then
  echo "오류: $PY 가 없습니다. 먼저 파이썬 3 을 설치하세요(Debian·Ubuntu: apt install python3)." >&2
  exit 1
fi
if ! "$PY" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 7) else 1)'; then
  echo "오류: Python 3.7 이상이 필요합니다(지금 $("$PY" -V 2>&1))." >&2
  exit 1
fi
# 한 번 시험으로 돌려 본다(보내지 않음). 실패하면 설치하지 않는다.
if ! "$PY" hostinfo_serial.py --dry --once --no-usb --no-lists >/dev/null; then
  echo "오류: hostinfo_serial.py 시험 실행이 실패했습니다(위 메시지 확인)." >&2
  exit 1
fi
install -m 755 hostinfo_serial.py /usr/local/bin/hostinfo_serial.py
install -m 644 hostinfo-serial.service /etc/systemd/system/hostinfo-serial.service
[ -f /etc/default/hostinfo-serial ] || echo "HOSTINFO_ARGS=" > /etc/default/hostinfo-serial
systemctl daemon-reload
systemctl enable hostinfo-serial.service
systemctl restart hostinfo-serial.service   # 이미 돌고 있으면 새 판으로
systemctl --no-pager --lines=3 status hostinfo-serial.service || true
