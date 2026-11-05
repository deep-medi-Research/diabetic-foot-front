# README의 흐름 애니메이션(docs/images/flow.gif)을 만듭니다. rsvg-convert와 ffmpeg가 필요합니다.
#   python3 tools/draw_flow_animation.py
# 자동 맞춤으로 밝은 장면을 찍는 한 번입니다. 실제 촬영은 약 33초이고 그림은 시간을 줄였습니다.
import shutil
import subprocess
import tempfile
from pathlib import Path

W, H = 1280, 590
FPS = 12
INK, MUTED, LINE, SURFACE, PANEL, DIM = "#1b1d21", "#6b7078", "#d8dbe0", "#ffffff", "#f4f5f7", "#b8bcc3"
REC, SETTLE, OK, BLUE = "#e5484d", "#d98a00", "#2f7d4f", "#3b6fd4"
FONT = 'font-family="Pretendard, Apple SD Gothic Neo, sans-serif"'

STAGES = [("1", "선언 읽기", INK, "specs/*.json 검증"), ("2", "미리보기", INK, "RGB, 열화상, PPG"),
          ("3", "안정화", SETTLE, "잠금, 75프레임"), ("4", "녹화", REC, "900장, FFV1"),
          ("5", "마무리", OK, "사이드카, 입력 확인"), ("6", "분석과 결과", BLUE, "약 50초, 결과 창")]
# 단계마다 그림 프레임 수
LENGTHS = [12, 14, 22, 46, 16, 30]


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;")


class Canvas:
    def __init__(self):
        self.out = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>',
                    '<defs><marker id="head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
                    f'orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="{MUTED}"/></marker></defs>']

    def text(self, x, y, s, size=15, color=INK, weight=400, anchor="start"):
        self.out.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" font-weight="{weight}" '
                        f'text-anchor="{anchor}" xml:space="preserve" {FONT}>{esc(s)}</text>')

    def rect(self, x, y, w, h, fill=SURFACE, stroke=LINE, r=12, width=1.5, opacity=1):
        self.out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" '
                        f'stroke-width="{width}" opacity="{opacity}"/>')

    def circle(self, x, y, r, fill, opacity=1):
        self.out.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" opacity="{opacity}"/>')

    def line(self, x1, y1, x2, y2, color=MUTED, width=2, arrow=True):
        m = ' marker-end="url(#head)"' if arrow else ""
        self.out.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{width}"{m}/>')

    def svg(self):
        return f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">' + "".join(self.out) + "</svg>"


def frame(stage, t):
    """stage: 지금 단계(0~5), t: 그 단계 안의 진행(0~1)"""
    c = Canvas()
    c.text(40, 46, "촬영 한 번의 흐름", 24, INK, 700)
    c.text(W - 40, 46, "자동 맞춤, 밝은 장면. 촬영 약 33초와 분석 약 50초를 줄여서 보여줌", 14, MUTED, 400, "end")

    # 단계 줄
    bw, gap, y0 = 189, 13, 72
    for i, (num, title, color, fact) in enumerate(STAGES):
        x = 40 + i * (bw + gap)
        active, done = i == stage, i < stage
        c.rect(x, y0, bw, 92, PANEL if active else SURFACE, color if active else LINE, 12, 3 if active else 1.5)
        c.circle(x + 28, y0 + 30, 14, color if (active or done) else DIM)
        c.text(x + 28, y0 + 36, num, 14, SURFACE, 700, "middle")
        c.text(x + 50, y0 + 37, title, 17, INK if (active or done) else DIM, 700)
        c.text(x + 18, y0 + 70, fact, 13, MUTED if (active or done) else DIM)
        if i < len(STAGES) - 1:
            c.line(x + bw + 2, y0 + 46, x + bw + gap - 2, y0 + 46, LINE if i >= stage else MUTED)

    # 앱 화면 줄: 상태, 숫자, 진행 막대
    sy = 200
    c.rect(40, sy, W - 80, 128, PANEL, PANEL)
    if stage == 0:
        pill, pill_color, big, sub, progress = "시작", MUTED, "선언 파일 읽는 중", "장치 See3CAM_24CUG, 1920x1080, 30fps, 900장, ffv1", 0
    elif stage == 1:
        pill, pill_color, big = "미리보기", MUTED, "목표 상태로 맞추는 중" if t < 0.5 else "미리보기"
        sub = "fps 60→30, 노출 자동, 잠금 해제, 다시 읽어 확인" if t < 0.5 else "RGB 30fps, 열화상 30fps 수신, PPG 72 bpm. 촬영 버튼을 기다림"
        progress = 0
    elif stage == 2:
        left = max(0, round(75 * (1 - t)))
        pill, pill_color = "안정화", SETTLE
        big = "잠그는 중" if t < 0.15 else f"안정화 {left}프레임 남음"
        sub = "열화상 셔터 교정 뒤 자동 교정 멈춤, PPG 기록 시작. 마지막 설정 변경부터 75프레임, 실제 fps 검사"
        progress = 0
    elif stage == 3:
        n = min(900, round(900 * t))
        pill, pill_color, big = "녹화", REC, f"{n} / 900장"
        sub = f"카메라 단 누락 0   저장 누락 0   열화상 {round(270 * t)} / 270장   PPG {round(15000 * t)}표본"
        progress = n / 900
    elif stage == 4:
        pill, pill_color, big = "완료", OK, "완료: 900장, 누락 0"
        sub = "좌우 MKV, 사이드카, 열화상 배열, PPG 파일 저장. 분석 입력 확인 통과" if t > 0.3 else "파일 닫는 중, 받은 프레임 전체로 누락 판정"
        progress = 1
    else:
        names = ["영상 읽기와 발 찾기", "맥박 신호 추출", "관류 계산", "판정", "결과 파일 저장"]
        bounds = [0.0, 0.56, 0.68, 0.70, 0.94, 1.0]
        k = max(i for i in range(5) if bounds[i] <= t)
        pill, pill_color = "분석", BLUE
        big = f"분석 중: {names[k]}  {round(100 * t)}%" if t < 0.98 else "결과 창: 판정, 관류지수, 그래프"
        sub = f"경과 {round(50 * t)}초, 약 {max(5, round(50 * (1 - t) / 5) * 5)}초 남음. 결과 창은 녹화 직후 열려 영상을 먼저 보임" if t < 0.98 \
            else "results 폴더로 요약, 그림, GIF 복사"
        progress = t
    c.rect(64, sy + 22, 120, 32, SURFACE, LINE, 16)
    c.circle(84, sy + 38, 6, pill_color)
    c.text(98, sy + 44, pill, 15, INK, 700)
    c.text(206, sy + 48, big, 26, INK, 700)
    c.text(64, sy + 90, sub, 15, MUTED)
    c.rect(64, sy + 106, W - 128, 6, LINE, LINE, 3)
    if progress > 0:
        bar = REC if stage == 3 else BLUE if stage == 5 else OK
        c.rect(64, sy + 106, (W - 128) * progress, 6, bar, bar, 3)

    # 스레드 셋
    ty = 360
    c.text(40, ty, "스레드 셋", 19, INK, 700)
    gap = 120
    lw, lh, ly = (W - 80 - 2 * gap) / 3, 112, ty + 18
    xs = [40, 40 + lw + gap, 40 + 2 * (lw + gap)]
    names = [("수신 스레드", "프레임만 받고 할 일을 나눔"), ("제어 스레드", "잠금, 설정, 다시 읽기"),
             ("쓰기 스레드", "FFV1 인코딩, 프레임별 기록")]
    busy = [True, stage in (1, 2, 4) and t < 0.5, stage in (3, 4)]
    for i, (name, desc) in enumerate(names):
        c.rect(xs[i], ly, lw, lh, SURFACE, INK if busy[i] else LINE, 12, 2 if busy[i] else 1.5)
        c.text(xs[i] + 20, ly + 36, name, 17, INK, 700)
        c.text(xs[i] + 20, ly + 66, desc, 14, MUTED)
        c.circle(xs[i] + lw - 26, ly + 30, 6, OK if busy[i] else DIM)
    # 수신과 제어 사이: 요청과 사건
    ax1, ax2 = xs[0] + lw + 6, xs[1] - 6
    request = (stage == 2 and t < 0.25) or (stage == 4 and 0.3 < t < 0.6)
    reply = (stage == 2 and 0.12 < t < 0.4) or (stage == 1 and t < 0.5)
    c.line(ax1, ly + 40, ax2, ly + 40, BLUE if request else LINE)
    label = "잠금 요청" if stage == 2 else "미리보기 목표로" if stage == 4 else "요청"
    c.text((ax1 + ax2) / 2, ly + 30, label, 13, BLUE if request else DIM, 400, "middle")
    c.line(ax2, ly + 80, ax1, ly + 80, BLUE if reply else LINE)
    c.text((ax1 + ax2) / 2, ly + 104, "설정 변경, 잠금 완료" if stage == 2 else "설정 변경 사건", 13,
           BLUE if reply else DIM, 400, "middle")
    # 수신에서 쓰기로 가는 프레임: 아래로 돌아가는 길 위에 점
    py = ly + lh + 34
    x_from, x_to = xs[0] + lw / 2, xs[2] + lw / 2
    c.out.append(f'<path d="M {x_from} {ly + lh + 2} L {x_from} {py} L {x_to} {py} L {x_to} {ly + lh + 6}" fill="none" '
                 f'stroke="{LINE}" stroke-width="2" marker-end="url(#head)"/>')
    c.text(W / 2, py + 26, "프레임 (버퍼 60장)", 13, MUTED, 400, "middle")
    if stage == 3:
        for k in range(6):
            p = (t * 9 + k / 6) % 1
            c.circle(x_from + (x_to - x_from) * p, py, 6, REC)
    return c.svg()


def main():
    root = Path(__file__).resolve().parent.parent
    target = root / "docs/images/flow.gif"
    work = Path(tempfile.mkdtemp())
    index = 0
    for stage, length in enumerate(LENGTHS):
        for i in range(length):
            svg = work / f"f{index:04d}.svg"
            svg.write_text(frame(stage, i / max(1, length - 1)), encoding="utf-8")
            subprocess.run(["rsvg-convert", str(svg), "-o", str(work / f"f{index:04d}.png")], check=True)
            index += 1
    # 마지막 화면을 1.5초 더 보여줍니다.
    for i in range(FPS * 3 // 2):
        shutil.copy(work / f"f{index - 1:04d}.png", work / f"f{index:04d}.png")
        index += 1
    pattern = str(work / "f%04d.png")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-framerate", str(FPS), "-i", pattern,
                    "-vf", "palettegen=max_colors=64:stats_mode=full", str(work / "palette.png")], check=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-framerate", str(FPS), "-i", pattern, "-i", str(work / "palette.png"),
                    "-lavfi", "paletteuse=dither=none", "-loop", "0", str(target)], check=True)
    shutil.rmtree(work)
    print(target, target.stat().st_size // 1024, "KB,", index, "frames")


if __name__ == "__main__":
    main()
