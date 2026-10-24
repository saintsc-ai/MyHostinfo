#!/bin/sh
# 펌웨어를 빌드해 bin/ 에 넣는다(PlatformIO 와 esptool 이 필요).
#   sh tools/build-bin.sh
# PIO·PY 환경변수로 pio·python 경로를 바꿀 수 있다.
set -e
cd "$(dirname "$0")/.."
PIO=${PIO:-pio}
PY=${PY:-python}
mkdir -p bin

(cd firmware/esp8266 && "$PIO" run -e smalltv -e nodemcuv2)
cp firmware/esp8266/.pio/build/smalltv/firmware.bin bin/SDP-hostinfo-smalltv.bin   # SmallTV 원래 펌웨어가 SDP 로 시작하는 이름만 받는다
cp firmware/esp8266/.pio/build/nodemcuv2/firmware.bin bin/nodemcu-hostinfo.bin

(cd firmware/esp32s3 && "$PIO" run)
B=firmware/esp32s3/.pio/build/esp32s3
APP0=${APP0:-$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin}
# 처음 굽기용 한 덩어리(0x0): 부트로더 0x0, 파티션 0x8000, boot_app0 0xe000, 앱 0x10000
"$PY" -m esptool --chip esp32s3 merge-bin -o bin/esp32s3-hostinfo-full.bin --flash-mode keep --flash-freq keep --flash-size 4MB \
  0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$APP0" 0x10000 "$B/firmware.bin"
cp "$B/firmware.bin" bin/esp32s3-hostinfo-ota.bin
ls -l bin
