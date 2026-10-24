# NodeMCU 화면 설치 방법

NodeMCU(ESP8266) 보드에 ST7789 화면 모듈을 점퍼선으로 이어 서버 상태 화면을 만듭니다. SmallTV와 같은 펌웨어(같은 화면)입니다.

NodeMCU는 쓰는 방법이 두 가지입니다.

- **USB 화면**: 서버(또는 PC)에 USB로 꽂아 두면 **그 컴퓨터 상태만** 보입니다. Wi-Fi 설정이 필요 없습니다. 서버 프로그램이 NodeMCU의 USB 칩(CP2102)을 스스로 찾습니다.
- **Wi-Fi 화면**: Wi-Fi를 설정하면 SmallTV처럼 같은 LAN의 서버들을 번갈아 보입니다.

![화면 모음](images/smalltv-screens.png)

## 필요한 파일

이 저장소에서 내려받습니다(초록 **Code** 버튼 → **Download ZIP**).

| 파일 | 쓰는 곳 |
|---|---|
| [`bin/nodemcu-hostinfo.bin`](../bin/nodemcu-hostinfo.bin) | NodeMCU에 구울 펌웨어(주소 `0x0`) |
| [`server/`](../server/) | 서버 프로그램과 설치 스크립트([server.md](server.md)) |

## 준비물

| 부품 | 설명 |
|---|---|
| **NodeMCU** | ESP8266(ESP-12E/F) 개발 보드. USB 칩이 **CP2102**인 것(흔한 NodeMCU v2·Amica)을 권합니다. CH340판도 굽기·Wi-Fi 화면은 되지만, USB 화면으로 쓸 때 서버 프로그램이 스스로 찾지 못하므로 `--port`를 줘야 합니다 |
| **ST7789 화면 모듈** | 핀이 `GND VCC SCL SDA RES DC CS BLK` 순서인 것. 1.54"(240×240)를 권합니다. 1.69"·1.14"·2.0"·2.25"도 됩니다 |
| **점퍼선 또는 납땜** | 8가닥. 보드·화면 모두 핀 헤더가 있으면 양끝이 암(구멍)인 점퍼선 |
| USB 데이터 케이블 | 보드에 맞는 것(Micro-USB가 흔함). 충전 전용 케이블은 안 됩니다 |

**더 싸게 사기**: 부품을 알리익스프레스 장바구니에 넣어 두고 **알뜰장터**에 가 보면, 같은 상품이나 비슷한 상품이 더 싼 값으로 뜨는 경우가 많습니다.

## 1. 배선

NodeMCU 앞면에는 `D1`·`D2` 같은 이름이 인쇄돼 있습니다. **화면 VCC는 3V3에 잇습니다.**

| 화면 핀 | NodeMCU | GPIO |
|---|---|---|
| GND | GND | |
| VCC | 3V3 | |
| SCL | D5 | 14 |
| SDA | D7 | 13 |
| RES | D1 | 5 |
| DC | D2 | 4 |
| CS | D8 | 15 |
| BLK | D6 | 12 |

화면의 SCL·SDA는 이름과 달리 SPI의 클럭·데이터입니다. CS 핀이 없는 모듈은 CS를 잇지 않습니다.

## 2. 펌웨어 굽기

보드를 컴퓨터에 USB로 꽂습니다. Windows는 장치 관리자 '포트(COM & LPT)'에 `Silicon Labs CP210x … (COMn)`이 보여야 합니다. 안 보이면 Silicon Labs CP210x 드라이버(CH340판이면 CH340 드라이버)를 설치합니다.

**브라우저로(크롬·엣지)**: [ESP Tool (esptool-js)](https://espressif.github.io/esptool-js/) → Baudrate `460800` → **Connect** → 포트 선택 → Flash Address `0x0`, 파일 `nodemcu-hostinfo.bin` → **Program** → 끝나면 보드의 **RST**를 누릅니다.

**명령으로**:

```sh
pip install esptool
python -m esptool --port <COMn 또는 /dev/ttyUSB0> -b 460800 write_flash 0x0 nodemcu-hostinfo.bin
```

NodeMCU는 굽기 모드로 저절로 들어갑니다. 안 되면 **FLASH** 버튼을 누른 채 **RST**를 눌렀다 뗀 뒤 다시 합니다.

## 3. 설정 (시리얼 명령)

시리얼 터미널을 **115200**으로 엽니다(esptool-js의 Console, Arduino IDE 시리얼 모니터의 `Newline`, PuTTY 등). 한 줄씩 보냅니다.

**화면 종류**(처음 한 번): 기본은 1.54"입니다. 다른 크기면 넣습니다. 넣으면 저장하고 다시 켜집니다.

```text
DISP 169
```

`154`(1.54") · `169`(1.69") · `114`(1.14") · `200`(2.0") · `225`(2.25" 막대). 백라이트가 거꾸로(평소 꺼지고 명령에서만 켜짐) 동작하는 모듈이면 `BLINV 1`.

**USB 화면으로만 쓰면 여기서 끝입니다.** 4번(서버 설치)으로 갑니다.

**Wi-Fi 화면으로 쓰려면** 이어서 넣습니다.

```text
CFG ssid <Wi-Fi 이름>
CFG pass <Wi-Fi 비밀번호>
CFG ota <OTA 비밀번호>
CFG RESTART
```

| 명령 | 뜻 |
|---|---|
| `CFG ssid` | Wi-Fi 이름(2.4GHz). 쉼표로 여러 개(비밀번호는 같아야 합니다). `CFG ssid -`면 Wi-Fi를 끕니다 |
| `CFG pass` | Wi-Fi 비밀번호 |
| `CFG ota` | 나중에 Wi-Fi로 펌웨어를 올릴 때 쓸 비밀번호 |
| `CFG geo 위도,경도,이름` | 날씨 위치(선택). 비우면 서버가 알려 준 위치, 그것도 없으면 IP로 찾은 위치. `off`면 날씨를 끕니다 |
| `CFG show <호스트 이름>` | 그 서버만 보이기(선택, 기본 `all`) |
| `CFG servers IP,IP…` | "여기 있음"을 보낼 서버 주소(선택). 서버가 안 보일 때만 넣습니다 |
| `CFG RESTART` | 다시 켜서 설정 적용 |
| `PING` | 응답 확인(`PONG hostinfo-display …`) |

## 4. 서버 프로그램 설치

[server.md](server.md)를 따라 설치합니다.

- **USB 화면**: NodeMCU를 꽂을 컴퓨터에 설치합니다. 인자에 `--broadcast`는 없어도 됩니다. 서버 프로그램이 3초마다 CP2102 보드를 찾아, 꽂혀 있으면 USB로 보냅니다. CH340판이면 `--port <포트> --baud 115200`을 줍니다.
- **Wi-Fi 화면**: 상태를 보일 컴퓨터마다 `--broadcast`로 설치합니다.

USB로 값이 오는 동안은 그 컴퓨터만 보이고, 끊기면 Wi-Fi로 받은 서버들을 보입니다.

## 나중에 펌웨어 업데이트하기

- **USB로**: 2번과 같습니다. 설정(EEPROM)은 남습니다.
- **Wi-Fi로**(`CFG ota`를 넣었을 때): [espota.py](https://raw.githubusercontent.com/esp8266/Arduino/master/tools/espota.py)로

  ```sh
  python espota.py -i <보드 주소> -p 8266 -a "<OTA 비밀번호>" -f nodemcu-hostinfo.bin
  ```

## 문제가 생기면

| 증상 | 할 일 |
|---|---|
| 포트가 안 보입니다 | 데이터 케이블인지 봅니다. CP210x(또는 CH340) 드라이버를 설치합니다. |
| 화면이 안 켜집니다 | 배선(특히 VCC·GND·BLK)을 봅니다. 백라이트만 반대면 `BLINV 1`. |
| 화면이 밀리거나 색이 이상합니다 | 크기에 맞게 `DISP` 값을 넣습니다. |
| 점퍼선일 때 화면 위쪽이 깨집니다 | 선을 짧게 합니다(펌웨어는 이미 SPI를 20MHz로 낮춰 둡니다). |
| USB로 꽂았는데 '응답 없음'만 나옵니다 | 그 컴퓨터에 서버 프로그램이 돌고 있는지, 다른 프로그램(시리얼 모니터)이 포트를 잡고 있지 않은지 봅니다. |
| 날짜·시계·날씨만 나오고 서버가 안 나옵니다(Wi-Fi) | ① 서버가 `--broadcast`로 도는지 ② 서버 방화벽 UDP 4211 ③ `CFG servers <서버 IP,…>` |
