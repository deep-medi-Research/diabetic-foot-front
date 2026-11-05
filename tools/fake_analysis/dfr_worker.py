"""가짜 관류 분석 프로그램입니다. 앱의 분석 상태 화면과 결과 창을 만들고 확인하는 데 씁니다.

앱이 실제 분석 프로그램을 띄우는 명령줄과 같은 인자로 띄웁니다.

    <python> dfr_worker.py --idle-exit 0 --presegment-threads 2 --prestream --variant fake

표준 입력의 JSON 한 줄을 받아 docs/analysis-contract.md의 줄 형식으로 답하고, 요청마다 같은 고정 결과 파일을 씁니다.
결과 값은 식으로 만든 숫자라 실제 분석과 관계가 없습니다. 파이썬 3.9 이상의 표준 라이브러리만 씁니다.

촬영 폴더 이름(요청의 name)이 fail, hang, crash, nosummary이면 분석 실패, 멈춤, 비정상 종료, 요약 없는 성공을 흉내 냅니다.
"""
import argparse
import binascii
import json
import math
import os
import struct
import sys
import threading
import time
import traceback
import zipfile
import zlib

WORKER_VERSION = "fake-1"
STAGE_MS_ENV = "FAKE_ANALYSIS_STAGE_MS"
DEFAULT_STAGE_MS = 200

FPS = 30.0
FRAMES = 900
WINDOW_S = 10
STRIDE_S = 2
WINDOWS = (int(FRAMES / FPS) - WINDOW_S) // STRIDE_S + 1
MAP_ROWS = 40
MAP_COLS = 15
OUTLINE_POINTS = 41
SIDES = ("left", "right")
REGIONS = ("toes", "fore", "mid", "heel")

FOOT_PI = (0.12, 0.15)
REGION_FACTOR = (1.15, 1.05, 1.0, 0.8)
WAVE_PI = (0.118, 0.147)
PULSE_AMPLITUDE = (0.13, 0.15)
PULSE_PHASE = (0.0, 0.35)
PULSE_HZ = 1.1


def _round(value):
    # 9자리로 맞춰 두면 운영체제마다 다른 sin, cos의 끝자리 차이가 결과에 남지 않습니다.
    return round(value, 9)


def _cycle(k):
    return 2 * math.pi * k / (WINDOWS - 1)


def pi_series(foot, region=None):
    factor = 1.0 if region is None else REGION_FACTOR[region]
    return [_round(FOOT_PI[foot] * factor * (1 + 0.08 * math.sin(_cycle(k)))) for k in range(WINDOWS)]


def window_times():
    return [float(k * STRIDE_S + WINDOW_S / 2) for k in range(WINDOWS)]


def template(foot):
    return [_round(math.sqrt(2) * math.sin(2 * math.pi * PULSE_HZ * i / FPS + PULSE_PHASE[foot])) for i in range(FRAMES)]


def template_corr():
    return [_round(0.92 + 0.04 * math.cos(_cycle(k))) for k in range(WINDOWS)]


def pulse(foot):
    return [_round(PULSE_AMPLITUDE[foot] * math.sin(2 * math.pi * PULSE_HZ * i / FPS + PULSE_PHASE[foot])) for i in range(FRAMES)]


def ac(foot):
    return [
        _round(p + 0.03 * math.sin(2 * math.pi * 0.2 * i / FPS) + 0.015 * math.sin(2 * math.pi * 4.7 * i / FPS))
        for i, p in enumerate(pulse(foot))
    ]


def map_valid(row, col):
    return not ((row < 3 or row >= MAP_ROWS - 2) and col in (0, MAP_COLS - 1))


def map_value(pi_by_foot, foot, window, row, col):
    if not map_valid(row, col):
        return math.nan
    return _round(pi_by_foot[foot][window] * (1.25 - 0.5 * row / (MAP_ROWS - 1)))


def map_top():
    return max(max(pi_series(foot)) for foot in range(2)) * 1.25


def half_width(u):
    # u는 발끝 0에서 뒤꿈치 1까지의 길이 위치이고, 폭은 발 길이를 1로 둔 값입니다.
    return 0.07 + 0.14 * math.sin(math.pi * math.sqrt(u))


def outline_point(i):
    u = i / (OUTLINE_POINTS - 1)
    return u, -half_width(u), half_width(u)


class NpyArray:
    """numpy 배열 하나입니다. data는 C 순서의 리틀 엔디언 바이트입니다."""

    def __init__(self, descr, shape, data):
        self.descr = descr
        self.shape = tuple(shape)
        self.data = data

    @staticmethod
    def numeric(descr, fmt, values, shape=None):
        shape = (len(values),) if shape is None else tuple(shape)
        if math.prod(shape) != len(values):
            raise ValueError("모양 %s과 값 수 %d가 다릅니다" % (shape, len(values)))
        return NpyArray(descr, shape, struct.pack("<%d%s" % (len(values), fmt), *values))

    @staticmethod
    def float64(values, shape=None):
        return NpyArray.numeric("<f8", "d", values, shape)

    @staticmethod
    def int32(values, shape=None):
        return NpyArray.numeric("<i4", "i", values, shape)

    @staticmethod
    def int64(values, shape=None):
        return NpyArray.numeric("<i8", "q", values, shape)

    @staticmethod
    def unicode(values, shape):
        # numpy 문자열 배열(<U길이)은 글자마다 UTF-32 리틀 엔디언 4바이트이고 짧은 값은 0으로 채웁니다.
        width = max(len(v) for v in values)
        data = b"".join(v.encode("utf-32-le").ljust(width * 4, b"\0") for v in values)
        return NpyArray("<U%d" % width, shape, data)

    def to_npy(self):
        if len(self.shape) == 0:
            shape = "()"
        elif len(self.shape) == 1:
            shape = "(%d,)" % self.shape[0]
        else:
            shape = "(%s)" % ", ".join(str(n) for n in self.shape)
        header = "{'descr': '%s', 'fortran_order': False, 'shape': %s, }" % (self.descr, shape)
        padded = (10 + len(header) + 1 + 63) // 64 * 64 - 10
        header = header.ljust(padded - 1) + "\n"
        return b"\x93NUMPY\x01\x00" + struct.pack("<H", len(header)) + header.encode("ascii") + self.data


def write_npz(path, arrays):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        for name, array in arrays:
            # 항목 시각과 만든 시스템을 고정해 같은 배열이면 같은 바이트가 나옵니다.
            info = zipfile.ZipInfo(name + ".npy", date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            archive.writestr(info, array.to_npy())


def perfusion_arrays(cuff_side):
    pi_by_foot = [pi_series(0), pi_series(1)]
    maps = [
        map_value(pi_by_foot, foot, k, r, c)
        for foot in range(2)
        for k in range(WINDOWS)
        for r in range(MAP_ROWS)
        for c in range(MAP_COLS)
    ]
    valid = [1 if map_valid(r, c) else 0 for r in range(MAP_ROWS) for c in range(MAP_COLS)]
    points = [outline_point(i) for i in range(OUTLINE_POINTS)]
    outline = [_round(p[axis]) for axis in range(3) for p in points]
    arrays = [
        ("time_s", NpyArray.float64(window_times())),
        ("maps", NpyArray.float64(maps, (2, WINDOWS, MAP_ROWS, MAP_COLS))),
        ("map_valid", NpyArray.int32(valid, (MAP_ROWS, MAP_COLS))),
        ("outline", NpyArray.float64(outline, (3, OUTLINE_POINTS))),
        ("template_left", NpyArray.float64(template(0))),
        ("template_right", NpyArray.float64(template(1))),
        ("template_corr", NpyArray.float64(template_corr())),
        ("n_light_ref", NpyArray.int64([6] * WINDOWS)),
        ("fps", NpyArray.float64([FPS], ())),
        # 실제 파일에도 있는 문자열 배열입니다. 앱의 npz 읽기는 지원하지 않는 dtype을 건너뜁니다.
        ("sides", NpyArray.unicode(SIDES, (2,))),
        ("cuff_side", NpyArray.unicode([cuff_side], ())),
    ]
    for foot in range(2):
        arrays.append(("pi_" + SIDES[foot], NpyArray.float64(pi_by_foot[foot])))
    for r, region in enumerate(REGIONS):
        for foot in range(2):
            arrays.append(("pi_%s_%s" % (region, SIDES[foot]), NpyArray.float64(pi_series(foot, r))))
    return arrays


def waveform_arrays():
    arrays = [("fps", NpyArray.float64([FPS], ()))]
    for foot in range(2):
        side = SIDES[foot]
        arrays.append(("ac_" + side, NpyArray.float64(ac(foot))))
        arrays.append(("pulse_" + side, NpyArray.float64(pulse(foot))))
        arrays.append(("pi_" + side, NpyArray.float64([WAVE_PI[foot]], ())))
    return arrays


def _mean(values):
    return sum(values) / len(values)


def _foot_mask(score):
    return {"method": "fake", "det_score": score, "sam_iou": score, "cells": 100}


def _cuff_level():
    return {
        "level": "0",
        "level_ko": "정상",
        "pad": False,
        "pad_probability": 0.02,
        "level_if_quality_ignored": "0",
        "quality_ok": True,
        "state_probabilities": {"0": 0.86, "V": 0.12, "D": 0.015, "O": 0.005},
        "rule": "가짜 분석기의 고정 결과",
        "features": {"hi_snr1_30": 18.0},
        "quality_snr_db": 11.5,
        "setting": {"mode": "manual", "exposure_step": -9, "gain": 1},
        "applicable": True,
    }


def _quality():
    action = "가짜 분석기의 고정 안내입니다. 실제 분석에서는 여기에 조치 문장이 옵니다."

    def item(code, name, severity, value, unit, message, item_action, reason):
        return {
            "code": code,
            "name_ko": name,
            "severity": severity,
            "value": value,
            "unit": unit,
            "threshold": "가짜 기준",
            "message_ko": message,
            "action_ko": item_action,
            "reason": reason,
        }

    return {
        "version": 2,
        "status": "check",
        "status_ko": "측정 완료 (확인 필요)",
        "action_ko": action,
        "items": [
            item("camera_setting", "카메라 설정", "ok", "manual, 노출 -9, 게인 1", "", "", "", None),
            item("foot_found", "발 찾기", "ok", "양발", "", "", "", None),
            item("pulse", "맥박 신호", "warn", 9.8, "dB", "가짜 분석기의 고정 경고입니다.", action, "other"),
        ],
        "thermal_contrast": {"left": 140.0, "right": 145.0},
    }


def summary(name, cuff_side, with_cuff_level, dfr):
    pi_left, pi_right = _mean(pi_series(0)), _mean(pi_series(1))
    per_side = {}
    for foot, side in enumerate(SIDES):
        per_side[side] = {"pi": _mean(pi_series(foot))}
        for r, region in enumerate(REGIONS):
            per_side[side]["pi_" + region] = _mean(pi_series(foot, r))
    return {
        "name": name,
        "fps": FPS,
        "n_frames": FRAMES,
        "window_s": WINDOW_S,
        "window_stride_s": STRIDE_S,
        "n_windows": WINDOWS,
        "cuff_side": cuff_side,
        "pi_left": _round(pi_left),
        "pi_right": _round(pi_right),
        "template_corr_lr": _round(_mean(template_corr())),
        "per_side": {s: {k: _round(v) for k, v in per_side[s].items()} for s in SIDES},
        "pi_ratio_left_right": _round(pi_left / pi_right),
        "classification": None,
        "cuff_level": _cuff_level() if with_cuff_level else None,
        "qc": _quality(),
        "foot_mask": {"left": _foot_mask(0.981), "right": _foot_mask(0.978)},
        "foot_cells": {"common": 92, "used": 90},
        "n_light_ref_min": 6,
        "dfr": dfr,
    }


# 그림은 16색 색표 하나를 GIF와 PNG가 함께 씁니다. 1~12는 값이 작은 쪽(파랑)에서 큰 쪽(빨강)으로 갑니다.
BACKGROUND = 0
GAP_COLOR = 13
WHITE = 15
RAMP_SIZE = 12
PALETTE = [
    (26, 28, 33),
    (48, 18, 59), (62, 73, 179), (70, 129, 241), (50, 182, 234), (27, 226, 196), (70, 248, 132),
    (156, 253, 60), (215, 226, 53), (250, 186, 57), (251, 128, 34), (228, 69, 11), (180, 22, 2),
    (60, 64, 72),
    (120, 120, 120),
    (255, 255, 255),
]

FOOT_HEIGHT = 140
MARGIN = 10
GAP = 12
BAR_HEIGHT = 12


def ramp(value, top):
    return 1 + min(max(round(value / top * (RAMP_SIZE - 1)), 0), RAMP_SIZE - 1)


class IndexedImage:
    def __init__(self, width, height, background):
        self.width = width
        self.height = height
        self.pixels = bytearray([background]) * (width * height)

    def set(self, x, y, color):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y * self.width + x] = color

    def fill_rect(self, x0, y0, x1, y1, color):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.set(x, y, color)


def draw_feet(color, extra_height=0):
    """두 발을 표준 발 틀 모양으로 그립니다. color(발, 행, 열)가 지도 칸의 색이고 None이면 계산하지 않는 칸의 색입니다."""
    widest = max(outline_point(i)[2] for i in range(OUTLINE_POINTS))
    foot_width = math.ceil(2 * widest * (FOOT_HEIGHT - 1)) + 1
    image = IndexedImage(2 * MARGIN + 2 * foot_width + GAP, 2 * MARGIN + FOOT_HEIGHT + extra_height, BACKGROUND)
    for foot in range(2):
        left = MARGIN + foot * (foot_width + GAP)
        for y in range(FOOT_HEIGHT):
            u = y / (FOOT_HEIGHT - 1)
            half = half_width(u)
            row = min(MAP_ROWS - 1, int(u * MAP_ROWS))
            for x in range(foot_width):
                across = -widest + x / (FOOT_HEIGHT - 1)
                if abs(across) > half:
                    continue
                col = min(MAP_COLS - 1, int((across + half) / (2 * half) * MAP_COLS))
                painted = color(foot, row, col)
                image.set(left + x, MARGIN + y, GAP_COLOR if painted is None else painted)
    return image


class Figures:
    def __init__(self):
        self.pi_by_foot = [pi_series(0), pi_series(1)]
        self.top = map_top()

    def _window_color(self, window):
        return lambda foot, row, col: (
            ramp(map_value(self.pi_by_foot, foot, window, row, col), self.top) if map_valid(row, col) else None
        )

    def map_frame(self, window):
        # 아래 막대 길이가 (창 번호 + 1) / 창 수라, 결과 창이 어느 장을 보이는지 눈으로 알 수 있습니다.
        image = draw_feet(self._window_color(window), BAR_HEIGHT)
        inner = image.width - 2 * MARGIN
        top = MARGIN + FOOT_HEIGHT + 4
        image.fill_rect(MARGIN, top, MARGIN + inner * (window + 1) // WINDOWS, top + BAR_HEIGHT - 6, WHITE)
        return image

    def segmentation(self):
        return draw_feet(lambda foot, row, col: WHITE)

    def template_cells(self):
        return draw_feet(lambda foot, row, col: 4 if (row // 2 + col // 3) % 2 == 0 else 8)

    def mean_map(self):
        def color(foot, row, col):
            if not map_valid(row, col):
                return None
            mean = _mean([map_value(self.pi_by_foot, foot, k, row, col) for k in range(WINDOWS)])
            return ramp(mean, self.top)

        return draw_feet(color)


GIF_MIN_CODE_SIZE = 4


def _lzw_blocks(pixels):
    # LZW 표가 커지기 전에 지우기 번호를 넣어 번호 폭을 5비트로 고정합니다. 압축은 되지 않지만 모든 GIF 디코더가 읽습니다.
    clear = 1 << GIF_MIN_CODE_SIZE
    width = GIF_MIN_CODE_SIZE + 1
    run = clear - 2
    data = bytearray()
    state = [0, 0]

    def emit(code):
        bits, count = state
        bits |= code << count
        count += width
        while count >= 8:
            data.append(bits & 0xFF)
            bits >>= 8
            count -= 8
        state[0], state[1] = bits, count

    emit(clear)
    for i, pixel in enumerate(pixels):
        if i > 0 and i % run == 0:
            emit(clear)
        emit(pixel)
    emit(clear + 1)
    if state[1] > 0:
        data.append(state[0] & 0xFF)
    out = bytearray([GIF_MIN_CODE_SIZE])
    for at in range(0, len(data), 255):
        chunk = data[at:at + 255]
        out.append(len(chunk))
        out += chunk
    out.append(0)
    return bytes(out)


def write_gif(path, frames, delay_centiseconds):
    first = frames[0]
    out = bytearray(b"GIF89a")
    out += struct.pack("<HH", first.width, first.height)
    out += bytes([0xF3, BACKGROUND, 0])
    out += b"".join(bytes(rgb) for rgb in PALETTE)
    out += b"\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00"
    for frame in frames:
        out += b"\x21\xf9\x04\x04" + struct.pack("<H", delay_centiseconds) + b"\x00\x00"
        out += b"\x2c\x00\x00\x00\x00" + struct.pack("<HH", frame.width, frame.height) + b"\x00"
        out += _lzw_blocks(frame.pixels)
    out.append(0x3B)
    with open(path, "wb") as f:
        f.write(out)


def _png_chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF)


def write_png(path, image):
    colors = [bytes(rgb) for rgb in PALETTE]
    raw = bytearray()
    for y in range(image.height):
        raw.append(0)
        raw += b"".join(colors[p] for p in image.pixels[y * image.width:(y + 1) * image.width])
    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_png_chunk(b"IHDR", header))
        f.write(_png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(_png_chunk(b"IEND", b""))


def summary_path(output, name):
    return os.path.join(output, name + "_summary.json")


def write_values(output, name, summary_json, cuff_side):
    os.makedirs(output, exist_ok=True)
    replace_json(summary_path(output, name), summary_json)
    write_npz(os.path.join(output, name + "_perfusion.npz"), perfusion_arrays(cuff_side))
    write_npz(os.path.join(output, name + "_waveforms.npz"), waveform_arrays())


def write_figures(output, name, with_gif):
    figures = Figures()
    write_png(os.path.join(output, name + "_segmentation.png"), figures.segmentation())
    write_png(os.path.join(output, name + "_template.png"), figures.template_cells())
    write_png(os.path.join(output, name + "_pi_map.png"), figures.mean_map())
    if with_gif:
        write_gif(os.path.join(output, name + "_perfusion.gif"), [figures.map_frame(k) for k in range(WINDOWS)], 80)


def replace_json(path, value):
    # 실제 분석 프로그램처럼 임시 파일에 쓴 뒤 이름을 바꿉니다. 앱이 요약 파일을 열어 두면 Windows에서 바꾸지 못합니다.
    temp = path + ".tmp"
    with open(temp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(value, f, indent=2)
    os.replace(temp, path)


def _text(request, key):
    # 빈 문자열은 없는 키와 같습니다.
    value = request.get(key)
    return value if isinstance(value, str) and value else None


def _flag(request, key, default=False):
    value = request.get(key)
    return value if isinstance(value, bool) else default


def _path_key(path):
    return os.path.normcase(os.path.abspath(path))


class Idle:
    """요청 없이 정한 시간이 지나면 `[dfr] idle-exit`을 찍고 끝냅니다. 분석 중에는 세지 않습니다."""

    def __init__(self, seconds, worker):
        self.seconds = seconds
        self.worker = worker
        self.touched = time.monotonic()
        self.busy = False

    def start(self):
        if self.seconds > 0:
            threading.Thread(target=self._watch, daemon=True).start()

    def touch(self):
        self.touched = time.monotonic()

    def _watch(self):
        while True:
            time.sleep(0.1)
            if self.busy or time.monotonic() - self.touched < self.seconds:
                continue
            self.worker.dfr("idle-exit")
            os._exit(0)


class Worker:
    def __init__(self, options, variant, stdin, stdout, stderr):
        self.options = options
        self.variant = variant
        self.stdin = stdin
        self.stdout = stdout
        self.stderr = stderr
        self.gate = threading.Lock()
        self.idle = Idle(options.idle_exit, self)
        self.presegment_on = options.presegment_threads is not None or options.prestream is not None
        self.prestream_on = options.prestream is not None
        self.stage_s = options.stage_ms / 1000.0
        self.preloaded = False
        self.any_presegment = False
        self.presegment_failure = None
        self.presegmented = set()
        self.prestream_jobs = set()

    def line(self, text):
        with self.gate:
            self.stdout.write(text + "\n")
            self.stdout.flush()

    def dfr(self, rest):
        self.line("[dfr] " + rest)

    def done(self, problem):
        self.dfr("done " + json.dumps({"ok": problem is None, "error": problem or ""}, ensure_ascii=False))

    def run(self):
        self.idle.start()
        ready = {"variant": self.variant["id"], "upstream_commit": self.variant["commit"], "worker_version": WORKER_VERSION}
        self.dfr("ready " + json.dumps(ready, ensure_ascii=False))
        while True:
            raw = self.stdin.readline()
            if raw == "":
                return 0
            text = raw.strip()
            if not text:
                continue
            self.idle.touch()
            self.handle(text)

    def handle(self, text):
        # 줄 앞의 BOM(U+FEFF)은 떼지 않습니다. 실제 분석 프로그램(json.loads)도 BOM이 붙은 줄을 읽지 못합니다.
        try:
            request = json.loads(text)
        except ValueError as error:
            self.done("요청을 읽지 못했습니다: %s" % error)
            return
        if not isinstance(request, dict):
            self.done("요청을 읽지 못했습니다: JSON 객체가 아닙니다")
        elif _flag(request, "presegment"):
            self.presegment(request)
        elif _flag(request, "preload"):
            self.preloaded = True
            self.dfr("preloaded")
        else:
            self.idle.busy = True
            try:
                self.analyze(request)
            finally:
                self.idle.busy = False
                self.idle.touch()

    def presegment(self, request):
        videos = [v for v in (_text(request, "left_video"), _text(request, "right_video")) if v]
        refused = None
        if not self.presegment_on:
            refused = "미리 분할이 꺼져 있습니다"
        elif not self.preloaded:
            self.presegment_failure = "model_not_loaded"
            refused = "분할 모델이 아직 올라 있지 않습니다"
        if refused is not None:
            self.dfr("presegmented " + json.dumps({"ok": False, "error": refused}, ensure_ascii=False))
            return
        self.any_presegment = True
        self.presegment_failure = None
        self.prestream_jobs.clear()
        if self.prestream_on and len(videos) == 2:
            self.prestream_jobs.update(_path_key(v) for v in videos)
        for video in videos:
            if os.path.isfile(video):
                self.presegmented.add(_path_key(video))
                found = {"video": video, "ok": True, "waited_s": 0.0, "seconds": 0.0, "found": True}
            else:
                self.presegment_failure = self.presegment_failure or "first_frame_unreadable"
                found = {"video": video, "ok": False, "error": "첫 장을 읽지 못했습니다", "reason": "first_frame_unreadable"}
            self.dfr("presegmented " + json.dumps(found, ensure_ascii=False))

    def stage(self, step, message, started, wait=True):
        self.line("[%d/5] %s  [%d s]" % (step, message, round(time.monotonic() - started)))
        if wait:
            time.sleep(self.stage_s)

    def analyze(self, request):
        started = time.monotonic()
        try:
            self.analyze_checked(request, started)
        except Exception:
            self.done(traceback.format_exc())

    def analyze_checked(self, request, started):
        problem, videos, name = self.check(request)
        if problem is not None:
            self.done(problem)
            return
        output = _text(request, "output")
        cuff_side = _text(request, "cuff_side") or "left"
        with_gif = _flag(request, "gif", True)
        defer = _flag(request, "defer_figures")
        with_cuff_level = _flag(request, "classify", True) and (_text(request, "template") or "v106w") == "v106w"
        dfr = {
            "variant": self.variant["id"],
            "upstream_commit": self.variant["commit"],
            "worker_version": WORKER_VERSION,
            "presegment": self.presegment_stamp(videos),
            "prestream": self.prestream_stamp(videos),
        }
        self.stage(0, "가짜 분석기: 준비", started)
        self.stage(1, "가짜 분석기: 영상 읽기 (%s)" % name, started)
        if name == "hang":
            time.sleep(60)
            return
        if name == "crash":
            self.stderr.write("Traceback (most recent call last)\nMemoryError\n")
            self.stderr.flush()
            os._exit(3)
        self.stage(2, "가짜 분석기: 맥박 신호", started)
        self.stage(3, "가짜 분석기: 관류 계산", started)
        self.stage(4, "가짜 분석기: 커프 단계", started)
        self.stage(5, "가짜 분석기: 결과 파일 -> %s/" % output, started, wait=False)
        if name == "fail":
            self.done("left.avi 가 없습니다: C:\\촬영\\left.avi\n두 번째 줄")
            return
        if name == "nosummary":
            self.done(None)
            return

        dfr["timing"] = {"done_s": round(time.monotonic() - started, 2)}
        result = summary(name, cuff_side, with_cuff_level, dfr)
        write_values(output, name, result, cuff_side)
        if not defer:
            write_figures(output, name, with_gif)
        self.line("Done  [%d s]" % round(time.monotonic() - started))
        self.done(None)
        if not defer:
            return

        time.sleep(self.stage_s)
        write_figures(output, name, with_gif)
        seconds = round(time.monotonic() - started, 2)
        try:
            dfr["timing"]["figures_s"] = seconds
            replace_json(summary_path(output, name), result)
        except OSError as error:
            self.dfr("stamp-failed %s" % error)
        self.dfr("figures " + json.dumps({"ok": True, "error": "", "seconds": seconds}, ensure_ascii=False))

    def check(self, request):
        """실제 분석 프로그램이 분석 전에 거절하는 요청을 같은 문장으로 거절합니다. 영상 머리글의 장 수 확인은 하지 않습니다."""
        rec_dir = _text(request, "rec_dir")
        output = _text(request, "output")
        if rec_dir is None or output is None:
            return "rec_dir 과 output 이 있어야 합니다.", [], ""
        left, right = _text(request, "left_video"), _text(request, "right_video")
        if (left is None) != (right is None):
            return "left_video 와 right_video 는 둘 다 주거나 둘 다 빼야 합니다.", [], ""
        videos = [left, right] if left is not None else [os.path.join(rec_dir, "left.avi"), os.path.join(rec_dir, "right.avi")]
        for side, video in zip(SIDES, videos):
            if not os.path.isfile(video):
                return "%s 영상이 없습니다: %s" % (side, video), videos, ""
        folder = os.path.abspath(rec_dir)
        name = _text(request, "name") or "%s_%s" % (
            os.path.basename(os.path.dirname(folder)),
            os.path.basename(folder),
        )
        if _flag(request, "pad_classifier"):
            return "이 패키지에는 이진 PAD 판정 모델이 없습니다. pad_classifier 를 빼고 요청해 주세요.", videos, name
        return None, videos, name

    def presegment_stamp(self, videos):
        if not self.presegment_on:
            return {"enabled": False}
        if all(_path_key(v) in self.presegmented for v in videos):
            reason = None
        else:
            reason = self.presegment_failure or ("first_frame_mismatch" if self.any_presegment else "not_requested")
        return {"enabled": True, "reason": reason}

    def prestream_stamp(self, videos):
        if not self.prestream_on:
            return {"enabled": False}
        keys = [_path_key(v) for v in videos]
        if not all(k in self.prestream_jobs for k in keys):
            return {"enabled": True, "used": False, "reason": "not_started"}
        self.prestream_jobs.difference_update(keys)
        used = all(k in self.presegmented for k in keys)
        for video in videos:
            ok = _path_key(video) in self.presegmented
            self.dfr(
                "prestreamed "
                + json.dumps(
                    {
                        "video": video,
                        "ok": ok,
                        "frames": FRAMES if ok else 0,
                        "error": None if ok else "미리 분할한 첫 장이 없습니다",
                        "reason": None if ok else "no_presegment",
                    },
                    ensure_ascii=False,
                )
            )
        return {"enabled": True, "used": used, "reason": None if used else "no_presegment"}


def load_variant(variants_dir, variant_id):
    known = sorted(
        d for d in os.listdir(variants_dir) if os.path.isfile(os.path.join(variants_dir, d, "VARIANT.json"))
    ) if os.path.isdir(variants_dir) else []
    if variant_id not in known:
        raise ValueError("--variant 는 %s 중 하나여야 합니다: '%s'" % (", ".join(known) or "(판 없음)", variant_id))
    with open(os.path.join(variants_dir, variant_id, "VARIANT.json"), encoding="utf-8") as f:
        info = json.load(f)
    upstream = info.get("upstream")
    commit = upstream.get("commit") if isinstance(upstream, dict) else None
    return {"id": variant_id, "commit": commit if isinstance(commit, str) else ""}


def parse_args(argv):
    parser = argparse.ArgumentParser(prog="dfr_worker", description="가짜 관류 분석 프로그램")
    parser.add_argument("--variant", required=True)
    parser.add_argument("--idle-exit", type=float, default=600.0)
    parser.add_argument("--presegment-threads", type=int, default=None)
    parser.add_argument("--prestream", nargs="?", type=int, const=0, default=None)
    parser.add_argument(
        "--stage-ms",
        type=int,
        default=int(os.environ.get(STAGE_MS_ENV, DEFAULT_STAGE_MS)),
        help="진행 단계 사이 대기(밀리초). 환경 변수 %s로도 줍니다" % STAGE_MS_ENV,
    )
    return parser.parse_args(argv)


def main(argv=None):
    options = parse_args(sys.argv[1:] if argv is None else argv)
    # 앱과는 BOM 없는 UTF-8로 주고받습니다. Windows의 기본 인코딩은 코드 페이지라 한글 경로가 깨집니다.
    sys.stdin.reconfigure(encoding="utf-8", errors="replace")
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    sys.stderr.reconfigure(encoding="utf-8", newline="\n")
    here = os.path.dirname(os.path.abspath(__file__))
    try:
        variant = load_variant(os.path.join(here, "variants"), options.variant)
    except ValueError as error:
        sys.stderr.write("ValueError: %s\n" % error)
        return 1
    return Worker(options, variant, sys.stdin, sys.stdout, sys.stderr).run()


if __name__ == "__main__":
    sys.exit(main())
