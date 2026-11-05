#!/bin/sh
# ffv1_64x32_40frames.mkv와 ffv1_yuv420p.mkv를 만듭니다. 64x32 UYVY 40장을 녹화 앱과 같은 설정(FFV1 level 3, 슬라이스 16, 슬라이스 CRC, GOP 1, 30fps, 4:2:2)으로 MKV에 담습니다.
# 화소 바이트는 Ffv1VideoReaderTests.ExpectedFrame과 같은 식입니다: ((i >> 1) + frame * 17 + (i >> 7) * 13) & 0xFF
# ffv1_yuv420p.mkv는 4:2:2가 아닌 영상을 열지 않는지 확인하는 4:2:0 영상 3장입니다.
# 필요한 것: ffmpeg, python3
set -eu
out="$(dirname "$0")/ffv1_64x32_40frames.mkv"
python3 -I -c '
import sys
for f in range(40):
    sys.stdout.buffer.write(bytes(((i >> 1) + f * 17 + (i >> 7) * 13) & 0xFF for i in range(64 * 32 * 2)))
' | ffmpeg -y -loglevel error -f rawvideo -pix_fmt uyvy422 -s 64x32 -r 30 -i - \
  -c:v ffv1 -level 3 -g 1 -slices 16 -slicecrc 1 -pix_fmt yuv422p "$out"
ffmpeg -y -loglevel error -f lavfi -i "color=c=gray:s=16x16:r=30:d=0.1" -c:v ffv1 -level 3 -g 1 -pix_fmt yuv420p \
  "$(dirname "$0")/ffv1_yuv420p.mkv"
