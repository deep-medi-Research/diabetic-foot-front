"""결과 창의 관류 지도가 관류 분석 프로그램 GIF를 그대로 보이는지 확인합니다.

    python check_result_gif.py --app <hwlab_app.exe> --shot <촬영 폴더> [--out <폴더>] [--min-score 0.9]
    python check_result_gif.py --image <결과 창 그림> --shot <촬영 폴더>   (앱을 실행하지 않고 저장한 그림만 검사)

앱을 결과 창만 띄우는 모드(--result, --screenshot)로 실행해 화면을 저장하고, 그 화면 안에서 GIF 첫 장의 왼쪽 절반(왼발 그림)과
가장 비슷한 곳을 여러 배율로 찾습니다(정규화 상관). 화면 없이 저장하는 창은 작아서 GIF 오른쪽이 창 밖으로 잘릴 수 있으므로
왼쪽 절반만 견줍니다. 점수가 --min-score 이상이면 통과입니다. 앱이 GIF 대신 다른 그림(예: 격자를 직접 칠한 지도)을
그리면 점수가 낮게 나옵니다. 장치는 열지 않습니다. 배포 폴더의 파이썬(bin/python, cv2와 PIL 포함)으로 실행합니다.
"""
import argparse
import glob
import os
import subprocess
import sys

import cv2
import numpy as np
from PIL import Image


def gif_first_frame(shot):
    paths = glob.glob(os.path.join(shot, "perfusion", "*_perfusion.gif"))
    if not paths:
        sys.exit(f"관류 지도 GIF가 없습니다: {shot}")
    with Image.open(paths[0]) as gif:
        gif.seek(0)
        frame = cv2.cvtColor(np.asarray(gif.convert("RGB")), cv2.COLOR_RGB2GRAY)
    return frame[:, : frame.shape[1] // 2]


def best_match(screen, frame):
    """GIF 첫 장을 화면 높이의 20~100% 배율로 줄여 가며 화면에서 가장 비슷한 곳의 점수와 배율을 돌려줍니다."""
    best = (-1.0, 0.0)
    for ratio in np.linspace(0.2, 1.0, 33):
        h = int(screen.shape[0] * ratio)
        w = int(frame.shape[1] * h / frame.shape[0])
        if h < 40 or w >= screen.shape[1] or h >= screen.shape[0]:
            continue
        small = cv2.resize(frame, (w, h), interpolation=cv2.INTER_AREA)
        score = float(cv2.minMaxLoc(cv2.matchTemplate(screen, small, cv2.TM_CCOEFF_NORMED))[1])
        if score > best[0]:
            best = (score, ratio)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app")
    ap.add_argument("--image")
    ap.add_argument("--shot", required=True)
    ap.add_argument("--out", default=".")
    ap.add_argument("--min-score", type=float, default=0.9)
    args = ap.parse_args()
    if args.image:
        shot_png = args.image
    elif args.app:
        os.makedirs(args.out, exist_ok=True)
        shot_png = os.path.join(args.out, "result_window.png")
        if os.path.exists(shot_png):
            os.remove(shot_png)
        # 결과 창이 GIF를 불러올 시간을 둔 뒤 저장하고 끝납니다.
        subprocess.run([args.app, "--result", args.shot, "--screenshot", shot_png, "--after", "5000"], timeout=120, check=False)
    else:
        sys.exit("--app 이나 --image 중 하나를 줍니다")
    if not os.path.exists(shot_png):
        sys.exit(f"앱이 결과 창 그림을 저장하지 않았습니다: {shot_png}")
    screen = cv2.cvtColor(cv2.imdecode(np.fromfile(shot_png, np.uint8), cv2.IMREAD_COLOR), cv2.COLOR_BGR2GRAY)
    score, ratio = best_match(screen, gif_first_frame(args.shot))
    print(f"GIF 첫 장(왼발 그림) 정합 점수 {score:.3f} (배율 {ratio:.2f}, 기준 {args.min_score})")
    if score < args.min_score:
        sys.exit("결과 창의 관류 지도가 관류 분석 프로그램 GIF와 다릅니다")
    print("결과 창이 관류 분석 프로그램 GIF를 보입니다")


if __name__ == "__main__":
    main()
