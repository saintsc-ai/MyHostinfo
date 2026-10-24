#!/bin/sh
# 맥에 사용자 서비스(launchd)로 설치. 저장소에서: sh hosts/hostinfo/install-mac.sh
# 지우기: launchctl bootout gui/$(id -u)/com.myhostinfo.hostinfo-serial; rm ~/Library/LaunchAgents/com.myhostinfo.hostinfo-serial.plist
set -e
cd "$(dirname "$0")"
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
