#!/bin/sh
# 맥에 사용자 서비스(launchd)로 설치. 저장소에서: sh hosts/hostinfo/install-mac.sh
# 지우기: launchctl bootout gui/$(id -u)/com.myhostinfo.hostinfo-serial; rm ~/Library/LaunchAgents/com.myhostinfo.hostinfo-serial.plist
set -e
cd "$(dirname "$0")"
# launchd 가 /usr/bin/python3 로 돌리므로 그것을 확인한다. 명령어 라인 개발자 도구가 없으면 이 파일은
# 설치 안내만 띄우고 실패한다.
PY=/usr/bin/python3
if ! "$PY" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 7) else 1)' 2>/dev/null; then
  echo "오류: $PY 로 Python 3.7 이상을 실행할 수 없습니다." >&2
  echo "      터미널에서 'xcode-select --install' 로 명령어 라인 개발자 도구를 설치한 뒤 다시 실행하세요." >&2
  exit 1
fi
if ! "$PY" hostinfo_serial.py --dry --once --no-usb --no-lists >/dev/null; then
  echo "오류: hostinfo_serial.py 시험 실행이 실패했습니다(위 메시지 확인)." >&2
  exit 1
fi
mkdir -p "$HOME/bin" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
install -m 755 hostinfo_serial.py "$HOME/bin/hostinfo_serial.py"
P="$HOME/Library/LaunchAgents/com.myhostinfo.hostinfo-serial.plist"
sed "s|__HOME__|$HOME|g" com.myhostinfo.hostinfo-serial.plist > "$P"
launchctl bootout "gui/$(id -u)/com.myhostinfo.hostinfo-serial" 2>/dev/null || true
sleep 2   # 바로 bootstrap 하면 'Bad request'(내리는 중)
launchctl bootstrap "gui/$(id -u)" "$P" || { sleep 3; launchctl bootstrap "gui/$(id -u)" "$P"; }
sleep 3
/usr/bin/python3 "$HOME/bin/hostinfo_serial.py" --dry --once
tail -3 "$HOME/Library/Logs/hostinfo-serial.log" 2>/dev/null || true
echo "설치함. 기록: ~/Library/Logs/hostinfo-serial.log"
