# 서버 설치 방법 — 화면에 상태를 보낼 컴퓨터

SmallTV·ESP32-S3·NodeMCU 화면에 상태를 보여 줄 컴퓨터마다 저장소 [`server/`](../server/) 폴더의 프로그램을 설치합니다. 리눅스·Windows·맥 모두 됩니다. 파이썬 3 표준 라이브러리만 쓰므로 따로 설치할 패키지는 없습니다.

설치하면 부팅·로그인 때 저절로 켜지고 늘 돕니다.

## 인자

모든 운영체제에서 같습니다.

| 인자 | 뜻 |
|---|---|
| `--broadcast` | Wi-Fi 화면으로 보냅니다. **Wi-Fi 화면(SmallTV·ESP32-S3, Wi-Fi로 쓰는 NodeMCU)이면 꼭 넣습니다.** USB로 꽂은 NodeMCU만 쓰면 없어도 됩니다 |
| `--geo 위도,경도,이름` | 이 장소의 위치를 화면에 알려 날씨를 맞춥니다. 한 장소에 서버 한 대만 주면 됩니다. 예: `--geo 37.566,126.978,Seoul` |
| `--no-usb` | USB 화면 보드를 찾지 않습니다. CP210x 칩을 쓰는 다른 USB 장치(Zigbee 동글 등)가 꽂혀 있으면 넣습니다 |
| `--no-lists` | Docker·Proxmox 목록을 보내지 않습니다 |
| `--dry --once` | 보내지 않고 보낼 내용을 한 번 출력합니다(시험용) |

위치를 주지 않으면 화면이 공인 IP로 대략의 위치를 찾습니다. 통신사에 따라 다른 구·시가 나올 수 있으니 `--geo`를 주는 편이 정확합니다. 위도·경도는 지도 앱에서 그 자리를 길게 누르면 나옵니다.

## 리눅스 (Debian·Ubuntu·Proxmox 등, systemd)

**root로 실행합니다.** 그래야 Docker 컨테이너와 Proxmox VM 목록을 읽을 수 있습니다.

1. `server` 폴더를 서버에 복사합니다. 예: `scp -r server <서버>:/tmp/hostinfo`
2. 서버에서 시험해 봅니다. `HOST …`로 시작하는 한 줄이 나오면 됩니다.

   ```sh
   python3 /tmp/hostinfo/hostinfo_serial.py --dry --once --no-usb
   ```

3. 인자를 정하고 설치합니다.

   ```sh
   echo 'HOSTINFO_ARGS=--broadcast --geo 37.566,126.978,Seoul' | sudo tee /etc/default/hostinfo-serial
   sudo sh /tmp/hostinfo/install-linux.sh
   ```

   `/usr/local/bin`에 복사하고 `hostinfo-serial` 서비스를 등록·시작합니다. 새 판으로 바꿀 때도 같은 명령을 씁니다.

- **확인**:
  - `systemctl status hostinfo-serial`
  - `journalctl -u hostinfo-serial -f` — 화면이 알려 오면 `display <화면 IP> <화면 id>`가 찍힙니다.
- **인자 바꾸기**: `/etc/default/hostinfo-serial`을 고친 뒤 `sudo systemctl restart hostinfo-serial`을 실행합니다.
- **시간대**: 서버 시간대가 UTC면 화면 시계가 9시간 틀립니다. 같은 파일에 `TZ=Asia/Seoul` 줄을 더하고 재시작합니다.
- **방화벽**: 켜져 있으면 UDP 4211 받기를 엽니다. ufw는 `sudo ufw allow 4211/udp`, firewalld는 `sudo firewall-cmd --permanent --add-port=4211/udp && sudo firewall-cmd --reload`입니다.
- **지우기**: `sudo systemctl disable --now hostinfo-serial`

## Windows 10·11 (작업 스케줄러)

1. [python.org](https://www.python.org/downloads/)에서 파이썬 3를 설치합니다. 설치 첫 화면에서 **Add python.exe to PATH**를 체크합니다.
2. `server` 폴더에서 PowerShell을 열고 실행합니다.

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install-windows.ps1
   ```

   `%USERPROFILE%\bin\hostinfo_serial.py`에 복사하고 작업 `hostinfo-serial`을 만듭니다. 로그인하면 창 없이 늘 돌고, 멈추면 1분 뒤 다시 켜집니다.

3. **방화벽**: 관리자 PowerShell에서 UDP 4211 받기를 엽니다.

   ```powershell
   New-NetFirewallRule -DisplayName "hostinfo UDP 4211" -Direction Inbound -Protocol UDP -LocalPort 4211 -Action Allow -Profile Private,Domain
   ```

   네트워크가 '공용'으로 잡혀 있으면 설정 → 네트워크에서 '개인'으로 바꾸거나, 위 명령의 `-Profile`을 `Any`로 합니다.

- **위치를 보내려면**: 작업 스케줄러 → `hostinfo-serial` → 동작 → 인수 끝에 ` --geo 37.566,126.978,Seoul`을 더합니다.
- **확인**: `Get-ScheduledTask hostinfo-serial`이 `Running`이면 됩니다. 기록은 `%LOCALAPPDATA%\hostinfo-serial.log`에 남습니다.
- **지우기**: `Unregister-ScheduledTask -TaskName hostinfo-serial -Confirm:$false`

## macOS (launchd)

1. 터미널에서 `server` 폴더로 가서 실행합니다.

   ```sh
   sh install-mac.sh
   ```

   `~/bin/hostinfo_serial.py`와 `~/Library/LaunchAgents/com.myhostinfo.hostinfo-serial.plist`를 만듭니다. 로그인하면 늘 돌고, 맥에 기본으로 있는 `/usr/bin/python3`를 씁니다. 처음이면 '명령어 라인 개발자 도구' 설치를 물을 수 있는데, 설치하면 됩니다.

2. **방화벽**: 시스템 설정 → 네트워크 → 방화벽이 켜져 있으면, `python3`의 들어오는 연결을 묻는 창에서 **허용**합니다.

- **위치를 보내려면**: `~/Library/LaunchAgents/com.myhostinfo.hostinfo-serial.plist`의 `<string>--broadcast</string>` 아래에 `<string>--geo</string>`와 `<string>37.566,126.978,Seoul</string>` 두 줄을 더합니다. 그 뒤 `launchctl bootout gui/$(id -u)/com.myhostinfo.hostinfo-serial; launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.myhostinfo.hostinfo-serial.plist`로 다시 읽힙니다. 설치 스크립트를 다시 돌리면 plist를 새로 써서 더한 줄이 지워지니 주의합니다.
- **노트북 Wi-Fi**: 맥이 Wi-Fi로 붙어 있으면, 그 네트워크의 '비공개 Wi-Fi 주소'를 끄는 것이 좋습니다. 켜 두면 공유기에서 주소가 바뀔 수 있습니다.
- **확인·기록**: `~/Library/Logs/hostinfo-serial.log`
- **지우기**: `launchctl bootout gui/$(id -u)/com.myhostinfo.hostinfo-serial; rm ~/Library/LaunchAgents/com.myhostinfo.hostinfo-serial.plist`

맥과 Windows는 Docker·VM 목록을 보내지 않고 서버 정보 화면만 보냅니다.

## 화면 보드 없이 송출 확인하기

화면 보드(SmallTV·ESP32-S3·NodeMCU)가 아직 없어도, 서버가 상태를 제대로 보내는지 확인할 수 있습니다.

1. **보낼 내용만 보기**: 서버에서 `python3 hostinfo_serial.py --dry --once`를 실행합니다. 보낼 `HOST …`(와 리눅스는 `LIST …`) 줄이 화면에 찍힙니다. 아무것도 보내지는 않습니다.
2. **실제로 받아 보기**: 같은 LAN의 PC(서버 자신이어도 됨)에서 **PC 창 앱** [`desktop/hostinfo_window.py`](../desktop/hostinfo_window.py)를 띄웁니다([desktop.md](desktop.md)).
   - 창은 화면 보드와 같은 방법으로 받습니다. UDP 4210을 받고, 4211로 "여기 있음"을 보냅니다.
   - 서버 이름·CPU·RAM 화면과 Docker·VM 목록이 차례로 나오면, 송출·방화벽·네트워크가 모두 맞는 것입니다.
   - 서버 쪽 기록에는 `display <PC IP> window-<PC 이름>`이 찍힙니다.
   - 날짜·시계·날씨만 나오면 서버 쪽 `--broadcast` 인자와 방화벽(UDP 4211), PC 쪽 방화벽(UDP 4210)을 확인합니다.
