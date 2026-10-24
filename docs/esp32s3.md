# ESP32-S3 화면 설치 방법

ESP32-S3 개발 보드에 작은 ST7789 화면을 점퍼선 8가닥으로 이어, **집이나 사무실 서버들의 상태를 번갈아 보여 주는 모니터**를 만듭니다. SmallTV판과 같은 화면이 나오고, 맨 앞에 보드 자신의 상태 화면이 하나 더 있습니다. 납땜 없이 점퍼선만으로 됩니다.

![화면 모음 — 서버 정보와 VM 목록, Docker 목록, 날씨·시계](images/smalltv-screens.png)

*화면 그림은 펌웨어의 그리기를 그대로 흉내 내 만든 것입니다(값은 예시, 1.54" 화면 기준).*

화면은 이 순서로 돕니다.

1. **보드 상태**(5초): 보드 id · Wi-Fi · IP · 메모리 · 켜진 시간
2. **서버 정보**(서버마다 8초): 이름 · IP · CPU 사용률·온도 · RAM · 디스크 · VM 수 · GPU · 켜진 시간
3. **목록**(리눅스 서버만, 쪽마다 6초): Docker 컨테이너와 Proxmox VM의 상태·CPU·메모리. 문제 있는 컨테이너는 빨간색으로 맨 위에 나옵니다.
4. **날씨·시계**(한 바퀴마다 5초)

서버가 없거나 끊기면 날짜·시계·날씨만 나옵니다.

## 필요한 파일

이 저장소에서 내려받습니다(초록 **Code** 버튼 → **Download ZIP**, 또는 파일마다 열어 **Download raw file**).

| 파일 | 쓰는 곳 |
|---|---|
| [`bin/esp32s3-hostinfo-full.bin`](../bin/esp32s3-hostinfo-full.bin) | **처음 굽기용** 한 덩어리 이미지(부트로더·파티션 포함, 주소 `0x0`) |
| [`bin/esp32s3-hostinfo-ota.bin`](../bin/esp32s3-hostinfo-ota.bin) | **나중에 Wi-Fi로 업데이트할 때** 쓰는 펌웨어 |
| [`server/`](../server/) | 서버에서 상태를 보내는 프로그램과 설치 스크립트([server.md](server.md)) |

## 준비물

| 부품 | 설명 |
|---|---|
| **ESP32-S3 개발 보드** | [알리익스프레스 "ESP32-S3 와이파이 블루투스 모듈"](https://ko.aliexpress.com/item/1005009330328373.html) — 옵션 **N16R8**로 시험했습니다. 이 펌웨어는 PSRAM을 쓰지 않고 플래시 4MB면 되므로 N8R2도 될 것으로 봅니다(시험하지 않음). USB-C가 두 개(`COM`·`USB`) 달린 DevKitC형입니다 |
| **ST7789 화면 모듈** | [알리익스프레스 "1.54 1.69 1.9인치 IPS TFT 디스플레이 SPI ST7789"](https://ko.aliexpress.com/item/1005010094030749.html) — 옵션 **1.54인치**(240×240)를 고릅니다. 1.69인치(240×280)도 됩니다(`CFG disp 169`). **1.9인치는 이 펌웨어가 지원하지 않습니다.** 핀은 `GND VCC SCL SDA RES DC CS BLK` 순서입니다 |
| **점퍼선 또는 납땜** | 화면과 보드를 8가닥으로 잇습니다. 점퍼선이면 [알리익스프레스 듀폰 점퍼선](https://ko.aliexpress.com/item/4000204858217.html)에서 양끝이 모두 **암(구멍)**인 것, 길이 **20cm**를 고릅니다(보드·화면 모두 핀 헤더가 달린 경우). 오래 둘 것이면 전선을 직접 납땜하는 편이 단단합니다 |
| USB-C 데이터 케이블 | 충전 전용 케이블은 굽기가 안 됩니다 |
| 컴퓨터 | 굽기와 설정용(Windows·맥·리눅스). 크롬 또는 엣지 브라우저 |

**더 싸게 사기**: 위 상품들을 알리익스프레스 장바구니에 넣어 두고 **알뜰장터**에 가 보면, 같은 상품이나 비슷한 상품이 더 싼 값으로 뜨는 경우가 많습니다.

## 어떻게 동작하나

```
[서버들] --(UDP 4210, 3초마다 상태)--> [ESP32-S3 화면]
[ESP32-S3 화면] --(UDP 4211, 5초마다 "여기 있음")--> [서버들] --> 그 화면 주소로 직접도 보냄
```

- 서버는 같은 LAN에 상태를 뿌립니다(브로드캐스트). 일반 공유기라면 그대로 받습니다.
- 기업용 무선 컨트롤러, 일부 메시 Wi-Fi, AP 격리 기능처럼 유선의 브로드캐스트가 Wi-Fi로 넘어가지 않는 곳에서는 보드 설정에 **서버 주소**를 적습니다(`CFG hsrv`, 3번).
- 보드를 컴퓨터에 USB로 꽂아 두면 Wi-Fi 없이 그 컴퓨터 상태만 보일 수도 있습니다(6번).

---

## 1. 배선

보드의 3.3V 쪽 핀 줄에 화면을 잇습니다. **화면 VCC는 3V3에 잇습니다**(5V에 이으면 레귤레이터가 없는 모듈은 망가집니다).

| 화면 핀 | ESP32-S3 |
|---|---|
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO6 |
| SDA | GPIO7 |
| RES | GPIO17 |
| DC | GPIO16 |
| CS | GPIO15 |
| BLK | GPIO18 |

- 화면의 SCL·SDA는 이름과 달리 I2C가 아니라 SPI의 클럭·데이터입니다.
- CS 핀이 없는 모듈은 CS를 잇지 않습니다. BLK 핀이 없는 모듈(2.0" 등)은 백라이트가 늘 켜져 있습니다.
- 보드마다 핀 배치가 조금씩 다릅니다. 핀 옆에 인쇄된 GPIO 번호로 맞춥니다. 흔한 배치는 왼쪽 줄이 `3V3 3V3 RST 4 5 6 7 15 16 17 18 …`입니다.

## 2. 펌웨어 굽기

보드의 **`COM`이라고 적힌 USB-C**(USB-시리얼 칩 쪽)로 컴퓨터에 꽂습니다. Windows 장치 관리자의 '포트(COM & LPT)'에 `USB-SERIAL CH343 (COMn)` 같은 포트가 생겨야 합니다. 안 보이면 WCH 사이트에서 CH343 드라이버를 설치합니다(보드에 따라 CP210x·CH340일 수도 있습니다).

### 가. 브라우저로 굽기 (설치할 것 없음, 크롬·엣지)

1. [ESP Tool (esptool-js)](https://espressif.github.io/esptool-js/)을 엽니다.
2. **Program** 쪽 Baudrate를 `460800`으로 두고 **Connect** → 보드 포트를 고릅니다.
3. Flash Address `0x0`, 파일 `esp32s3-hostinfo-full.bin`을 고르고 **Program**을 누릅니다.
4. 끝나면 **Disconnect** 후 보드의 **RST** 버튼을 누릅니다.

### 나. 명령으로 굽기

파이썬이 있으면 `pip install esptool` 뒤:

```sh
python -m esptool --chip esp32s3 --port <COMn 또는 /dev/ttyUSB0> -b 460800 write_flash 0x0 esp32s3-hostinfo-full.bin
```

**Connect가 안 되면** 굽기 모드로 직접 넣습니다: **BOOT 버튼을 누른 채 RST를 눌렀다 떼고**, 그다음 BOOT를 뗍니다. 그 뒤 다시 Connect합니다.

## 3. 설정 (시리얼 명령)

보드는 USB 시리얼로 한 줄 명령을 받습니다. 속도는 **921600**입니다.

**시리얼 터미널 열기** — 아래 중 편한 것을 씁니다.
- 위 esptool-js 페이지 아래쪽 **Console**: Baudrate `921600` → Start
- Arduino IDE 시리얼 모니터: `921600 baud`, 줄 끝 `Newline`
- PuTTY(Windows): Serial, COM 포트, 속도 `921600`

명령을 한 줄씩 보냅니다. 각 줄 뒤에 `OK cfg …`가 나옵니다.

```
CFG ssid <Wi-Fi 이름>
CFG pass <Wi-Fi 비밀번호>
CFG disp 154
CFG ota <OTA 비밀번호>
CFG RESTART
```

| 명령 | 뜻 |
|---|---|
| `CFG ssid` | Wi-Fi 이름(2.4GHz). 여러 곳에서 쓰려면 쉼표로 여러 개(예: `home,office`, 비밀번호는 같아야 합니다). 그중 신호가 센 곳에 붙습니다 |
| `CFG pass` | Wi-Fi 비밀번호 |
| `CFG disp` | 화면 종류. `154`(1.54") · `169`(1.69") · `114`(1.14") · `200`(2.0") · `225`(2.25" 막대). **넣지 않으면 화면이 켜지지 않습니다** |
| `CFG ota` | 나중에 Wi-Fi로 펌웨어를 올릴 때 쓸 비밀번호. 직접 정하고 적어 둡니다 |
| `CFG hinfo off` | 서버 화면을 끄고 보드 상태만 보입니다(선택) |
| `CFG geo 위도,경도,이름` | 날씨 위치(선택). 비우면 서버가 알려 준 위치, 그것도 없으면 IP로 찾은 위치를 씁니다. 예: `CFG geo 37.566,126.978,Seoul` |
| `CFG hsrv IP,IP…` | "여기 있음"을 보낼 서버 주소(선택). 서버가 안 보일 때만 넣습니다 |
| `CFG` | 지금 설정 보기(비밀번호는 가려 나옵니다) |
| `CFG <키>` | 그 설정 지우기 |
| `CFG RESTART` | 다시 켜서 설정 적용 |

다시 켜지면 화면에 보드 상태가 나오고, Wi-Fi에 붙으면 IP 주소가 보입니다.

**권장**: 공유기 설정에서 보드와 서버들에 **DHCP 고정 할당**(주소 예약)을 해 둡니다.

## 4. 서버에 보내는 프로그램 설치

화면에 나올 컴퓨터마다 설치합니다. 리눅스·Windows·맥 설치 방법은 **[server.md](server.md)**에 있습니다. 핵심만 적으면 이렇습니다.

- 리눅스: `/etc/default/hostinfo-serial`에 `HOSTINFO_ARGS=--broadcast`를 넣고 `sudo sh install-linux.sh`
- Windows: `install-windows.ps1` 실행 후 방화벽 UDP 4211 열기
- 맥: `sh install-mac.sh`

## 5. 확인

- 시리얼에 `NETS`를 보내면 보드가 받고 있는 서버 목록이 나옵니다.
- 서버 쪽 기록에 `display <보드 IP> esp32-xxxxxx`가 보이면 서로 찾은 것입니다.
- **BOOT 버튼**을 누르면 보드 상태 화면이 30초 동안 나옵니다.

### 보드 상태 화면 읽는 법

| 줄 | 뜻 |
|---|---|
| `esp32-xxxxxx` | 보드 id(MAC 끝 6자리) |
| Wi-Fi | 붙은 SSID와 신호 세기(dBm, -70보다 크면 좋음) |
| IP | 보드 주소 |
| `servers N` | 지금 받고 있는 서버 수 |
| `weather …` | 받은 기온(날씨를 아직 못 받았으면 `-`) |
| `RAM` · `up` | 남은 메모리, 켜진 시간 |

## 6. USB로 꽂은 컴퓨터만 보이기 (선택)

Wi-Fi 없이, 보드를 꽂은 컴퓨터 한 대의 상태만 보일 수도 있습니다. 그 컴퓨터의 서버 프로그램 인자에 보드 포트와 속도를 줍니다.

- 리눅스: `HOSTINFO_ARGS=--port /dev/ttyACM0 --baud 921600`(포트 이름은 `ls /dev/ttyACM* /dev/ttyUSB*`로 확인)
- Windows: 작업 스케줄러 `hostinfo-serial` 인수에 `--port COM5 --baud 921600`
- 맥: plist 인자에 `--port`, `/dev/cu.wchusbserial…`, `--baud`, `921600`

USB로 값이 오는 동안은 그 컴퓨터만 보이고, 끊기면 Wi-Fi로 받은 서버들을 다시 보입니다. 보드 포트를 찾는 명령: `python hostinfo_serial.py --list`.

---

## 나중에 펌웨어 업데이트하기 (Wi-Fi)

`CFG ota`로 비밀번호를 넣어 두었으면 USB 없이 올릴 수 있습니다. 필요한 것은 `espota.py` 하나입니다(Arduino ESP32 코어에 들어 있는 OTA 도구).

1. [espota.py](https://raw.githubusercontent.com/espressif/arduino-esp32/master/tools/espota.py)를 받습니다.
2. 실행합니다(**`-ota.bin`** 파일, 포트 `3232`).

   ```sh
   python espota.py -i <보드 주소> -p 3232 -a "<OTA 비밀번호>" -f esp32s3-hostinfo-ota.bin
   ```

3. 끝나면 보드가 다시 켜집니다. 설정은 그대로 남습니다.

USB로 `-full.bin`을 다시 구우면(2번) **설정이 지워집니다**. 이 이미지가 설정 저장 영역까지 덮어쓰기 때문입니다. 그 뒤 3번 설정을 다시 합니다. 설정을 남긴 채 USB로 올리려면 `-ota.bin`을 주소 `0x10000`에 굽습니다(`write_flash 0x10000 esp32s3-hostinfo-ota.bin`).

## 문제가 생기면

| 증상 | 할 일 |
|---|---|
| 포트가 안 보입니다 | 데이터 케이블인지, `COM` 쪽 USB-C에 꽂았는지 봅니다. CH343·CP210x·CH340 드라이버를 설치합니다. |
| 굽기가 `Failed to connect`로 실패합니다 | BOOT를 누른 채 RST를 눌렀다 떼고, BOOT를 뗀 뒤 다시 합니다. 속도를 `115200`으로 낮춰 봅니다. |
| 구웠는데 켜지지 않고 시리얼에 같은 글이 반복됩니다 | 굽기가 덜 됐을 수 있습니다. 한 덩어리 이미지(`-full.bin`)를 주소 `0x0`에 다시 굽습니다. |
| 화면이 안 켜집니다 | `CFG disp`를 넣고 `CFG RESTART` 했는지 봅니다. 시리얼에 `DISP <종류> ok`가 나와야 합니다. 배선(특히 VCC·GND·BLK)을 확인합니다. |
| 화면이 하얗거나 색이 반대입니다 | 화면 종류를 잘못 넣었습니다. 크기에 맞는 `CFG disp` 값을 넣습니다. |
| 화면이 밀려 있거나 가장자리에 줄이 있습니다 | 1.69"는 `169`, 1.54"는 `154`처럼 크기를 맞춥니다. |
| 시리얼에 글자가 깨져 나옵니다 | 속도가 `921600`인지 봅니다. |
| 날짜·시계·날씨만 나오고 서버가 안 나옵니다 | ① 서버 프로그램이 `--broadcast`로 돌고 있는지 ② 서버 방화벽의 UDP 4211 ③ 그래도 안 되면 `CFG hsrv <서버 IP,…>` |
| Wi-Fi에 못 붙습니다 | 2.4GHz인지, SSID·비밀번호를 확인합니다(`CFG`로 설정 보기). |
| OTA가 `Authenticating...FAIL` | OTA 비밀번호가 다릅니다. USB로 `CFG ota <비밀번호>`를 다시 넣습니다. |

## 참고

- 펌웨어는 Arduino-ESP32(2.0.x)와 Adafruit ST7789·GFX 라이브러리로 만들었습니다. 화면은 SPI3(HSPI)를 씁니다.
- **CC1101 무선 모듈도 달 수 있습니다.** 화면은 SPI3(HSPI)를 쓰고 다른 SPI(FSPI)의 핀은 비워 두었으므로, 아래처럼 CC1101을 함께 연결할 수 있습니다. CC1101은 **3.3V 전용**입니다. 이 펌웨어에는 CC1101을 쓰는 기능이 없으니, 쓰려면 직접 기능을 더해야 합니다. 쓸 때는 국내 전파 규정에 맞는 주파수와 용도로만 씁니다.

  | CC1101 핀 | ESP32-S3 |
  |---|---|
  | GND | GND |
  | VCC | 3V3 |
  | GDO0 | GPIO4 |
  | GDO2 | GPIO5 |
  | CSN | GPIO10 |
  | MOSI | GPIO11 |
  | SCK | GPIO12 |
  | MISO | GPIO13 |
