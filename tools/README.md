# tools

**`check_result_gif.py`만 파이썬 패키지(NumPy, OpenCV, Pillow)가 필요하고, 나머지 스크립트는 파이썬 표준 라이브러리만 씁니다.** 가짜 분석 프로그램은 파이썬 3.9 이상이 필요합니다. 필요한 패키지는 [`requirements.txt`](requirements.txt)에 있습니다.

```bash
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
```

| 스크립트 | 하는 일 | 파이썬 패키지 | 외부 프로그램 |
| --- | --- | --- | --- |
| `draw_flow.py` | 촬영 한 번의 흐름 그림(SVG) | 없음 | `rsvg-convert`(PNG로 바꿀 때) |
| `draw_flow_animation.py` | 흐름 애니메이션(GIF) | 없음 | `rsvg-convert`, `ffmpeg` |
| `check_result_gif.py` | 결과 창이 분석 GIF를 그대로 보이는지 확인 | NumPy, OpenCV, Pillow | 앱(`hwlab_app`) |
| `fake_analysis/dfr_worker.py` | 가짜 분석 프로그램 | 없음 | 없음 |

- **`draw_flow.py`, `draw_flow_animation.py`**<br>`docs/images/`의 그림 원본입니다. 코드가 바뀌면 스크립트를 고쳐 다시 그립니다. 실행 방법은 각 파일 맨 위 주석에 있습니다. `rsvg-convert`는 macOS에서 `brew install librsvg`로 설치합니다
- **`check_result_gif.py`**<br>앱의 `--result`와 `--screenshot`으로 결과 창을 저장해, 그 안에 분석 GIF 첫 장이 있는지 찾습니다. 사용법은 파일 맨 위 설명에 있습니다. OpenCV는 창을 띄우지 않으므로 `opencv-python-headless`면 됩니다. `--shot`은 분석 판 폴더(`<촬영 폴더>/perfusion/<판>/`)의 `_perfusion.gif`를 찾습니다. 판이 여럿이면 처음 찾은 판을 씁니다
- **`fake_analysis/`**<br>사용법은 [`fake_analysis/README.md`](fake_analysis/README.md)에 있습니다
