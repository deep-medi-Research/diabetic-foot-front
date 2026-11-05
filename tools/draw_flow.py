# README의 전체 흐름 그림을 그립니다.
#   python3 tools/draw_flow.py && rsvg-convert -z 2 docs/images/flow.svg -o docs/images/flow.png
from pathlib import Path

W, H = 1600, 1060
INK, MUTED, LINE, SURFACE, PANEL = "#1b1d21", "#6b7078", "#d8dbe0", "#ffffff", "#f4f5f7"
REC, SETTLE, OK = "#e5484d", "#d98a00", "#2f7d4f"
FONT = 'font-family="Pretendard, Apple SD Gothic Neo, Malgun Gothic, sans-serif"'
out = []


def text(x, y, s, size=15, color=INK, weight=400, anchor="start"):
    out.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" font-weight="{weight}" '
               f'text-anchor="{anchor}" {FONT}>{s}</text>')


def lines(x, y, items, size=14, color=MUTED, gap=21):
    for i, s in enumerate(items):
        text(x, y + i * gap, s, size, color)


def box(x, y, w, h, fill=SURFACE, stroke=LINE, r=12, width=1.5):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>')


def arrow(x1, y1, x2, y2, color=MUTED, dash=False):
    d = ' stroke-dasharray="5 5"' if dash else ""
    out.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="2"{d} marker-end="url(#head)"/>')


out.append(f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>')
out.append('<defs><marker id="head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">'
           f'<path d="M0,0 L10,5 L0,10 z" fill="{MUTED}"/></marker></defs>')

text(48, 62, "촬영 한 번의 흐름", 30, INK, 700)
text(48, 94, "촬영 선언 파일 하나로 시작하고, 무엇을 어떤 순서로 바꿀지는 core가 정하며, platform과 화면은 실행하고 그리기만 합니다. RGB, 열화상, PPG를 같은 PC 시계로 맞춥니다", 16, MUTED)

# 1. 단계
stages = [
    ("1", "선언 읽기", INK, ["specs/*.json", "장치, 크기, 설정 방식", "카메라 실측값(4, 3, 75프레임)", "목표 900장, FFV1", "열화상 기록 9Hz"], "core: 선언 검증"),
    ("2", "미리보기", INK, ["RGB, 열화상, PPG 수신", "좌우 발 상자(왼발이 왼쪽)", "실시간 설정값, 실제 fps", "PPG 심박수, SpO2", "환자 정보, 커프 압력 %"], "core: 명령 계획"),
    ("3", "안정화", SETTLE, ["촬영 버튼, PPG 기록 시작", "자동 맞춤 잠금", "열화상 셔터 교정 뒤 멈춤", "설정 변경 뒤 75프레임", "시작 직전 실제 fps 검사"], "core: 반영, 안정화 판정"),
    ("4", "녹화", REC, ["RGB 900장이면 끝", "좌우 발 MKV(FFV1, fps 기록)", "열화상 원시값 9Hz", "PPG 500Hz", "프레임마다 공통 시각"], "core: 촬영 흐름"),
    ("5", "마무리", OK, ["사이드카(JSON), 누락 판정", "열화상 좌우 배열(npy)", "PPG 파일(_ppg.csv)", "분석 입력 확인", "설정 복원"], "core: 누락, 입력 판정"),
    ("6", "분석과 결과", "#3b6fd4", ["결과 창 바로 열림", "상주 관류 분석 프로그램 약 50초", "단계, %, 남은 시간 표시", "판정, PI, 그래프, 재생", "results 폴더로 복사"], "platform: 관류 분석 프로그램 실행"),
]
x0, y0, bw, bh, gap = 48, 130, 236, 232, 17.6
for i, (num, title, color, items, who) in enumerate(stages):
    x = x0 + i * (bw + gap)
    box(x, y0, bw, bh)
    out.append(f'<circle cx="{x + 30}" cy="{y0 + 32}" r="15" fill="{color}"/>')
    text(x + 30, y0 + 38, num, 15, SURFACE, 700, "middle")
    text(x + 56, y0 + 39, title, 20, INK, 700)
    lines(x + 22, y0 + 76, items, 13.5, MUTED, 22)
    text(x + 22, y0 + bh - 18, who, 13, color, 600)
    if i < len(stages) - 1:
        arrow(x + bw + 2, y0 + bh / 2, x + bw + gap - 2, y0 + bh / 2)

# 2. 예외
ey = y0 + bh + 26
box(48, ey, 1504, 124, PANEL, PANEL)
text(72, ey + 34, "예외", 17, INK, 700)
text(140, ey + 34, "연결 끊김", 15, INK, 600)
text(220, ey + 34, "녹화 중이면 그때까지 쓴 파일과 사이드카를 닫음. 0.5초마다 다시 열고, 열리면 공장 기본값으로 돌아간 설정을 선언대로 다시 맞춤", 14, MUTED)
text(140, ey + 66, "fps 불일치", 15, INK, 600)
text(226, ey + 66, "실제 프레임 간격이 선언과 5% 넘게 다르면 녹화하지 않고 이유를 표시. 어두우면 자동 맞춤이 노출을 늘려 15fps로 떨어짐(조명으로 해결, 게인은 잡음을 키움)", 14, MUTED)
text(140, ey + 98, "분석 거부", 15, INK, 600)
text(226, ey + 98, "영상 머리글의 장 수가 녹화 기록과 다르면 분석하지 않고 이유를 표시. 다음 촬영의 안정화와 녹화 중에는 새 분석을 시작하지 않음", 14, MUTED)

# 3. 스레드
ty = ey + 160
text(48, ty, "스레드 셋 (platform Capture). 열화상과 PPG는 각자 받는 스레드가 따로 있고 같은 PC 시계를 씀", 21, INK, 700)
lanes = [
    ("수신", ["카메라에서 프레임만 받음", "촬영 흐름에 사건을 넣고 할 일을 나눔", "카메라 제어는 하지 않음(제어는 수백 ms)"]),
    ("제어", ["잠금, 설정, 다시 읽기", "목표 상태 맞추기", "바꾼 설정을 사건으로 되돌려 보냄"]),
    ("쓰기", ["좌우 발 FFV1 인코딩과 파일 쓰기", "프레임별 밝기, 포화 화소 측정", "사이드카와 누락 판정"]),
]
lw, lh, ly = 420, 150, ty + 24
for i, (name, items) in enumerate(lanes):
    x = 48 + i * (lw + 122)
    box(x, ly, lw, lh)
    text(x + 22, ly + 38, name + " 스레드", 19, INK, 700)
    lines(x + 22, ly + 72, items)
arrow(48 + lw + 4, ly + 60, 48 + lw + 118, ly + 60)
text(48 + lw + 61, ly + 50, "잠금 요청", 13, MUTED, 400, "middle")
arrow(48 + lw + 118, ly + 110, 48 + lw + 4, ly + 110)
text(48 + lw + 61, ly + 130, "설정 변경 사건", 13, MUTED, 400, "middle")
# 수신에서 쓰기로 가는 프레임은 제어 스레드를 거치지 않으므로 아래로 돌아갑니다.
out.append(f'<path d="M {48 + lw / 2} {ly + lh + 2} L {48 + lw / 2} {ly + lh + 26} L {48 + 2 * (lw + 122) + lw / 2} {ly + lh + 26} '
           f'L {48 + 2 * (lw + 122) + lw / 2} {ly + lh + 6}" fill="none" stroke="{MUTED}" stroke-width="2" marker-end="url(#head)"/>')
text(W / 2, ly + lh + 50, "프레임 (버퍼 60장. 버퍼가 차면 수신을 멈추지 않고 저장 누락으로 셈)", 14, MUTED, 400, "middle")

# 4. 층
cy = ly + lh + 92
text(48, cy, "층 구조 (의존은 아래로만)", 21, INK, 700)
layers = [
    ("app", "Qt Quick 화면. ViewModel(촬영, 분석, 결과 창, 조명, PPG)이 바뀐 속성만 알리고, QML은 그리기만 함"),
    ("platform", "Capture(스레드 셋), Windows 카메라와 열화상 SDK, FFV1 쓰기와 읽기, PPG 장치, LED, 관류 분석 프로그램 실행, 결과 파일 읽기, 가짜 장치"),
    ("core", "flow(촬영 흐름, 설정 계획, 촬영 선언), timing(누락, 안정화, 공통 시간축), pixel(밝기, 자르기, 변환), record(사이드카, 촬영 폴더, 분석 입력)"),
]
for i, (name, desc) in enumerate(layers):
    y = cy + 20 + i * 52
    box(48, y, 1504, 42, PANEL if name == "core" else SURFACE)
    text(72, y + 28, name, 17, INK, 700)
    text(190, y + 28, desc, 14, MUTED)

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">' + "".join(out) + "</svg>\n")
Path(__file__).resolve().parent.parent.joinpath("docs/images/flow.svg").write_text(svg, encoding="utf-8")
