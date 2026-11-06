#!/usr/bin/env python3
"""촬영 폴더 하나를 읽어 docs/recording-checklist.md 2~8절을 항목마다 판정합니다.

    python3 tools/check_shot.py <촬영 폴더> [--json]

항목마다 PASS, FAIL, WARN, SKIP 중 하나를 냅니다. 출처가 코드나 결정인 항목은 어기면 FAIL이고, 미정인 항목은 어겨도
WARN입니다. 자료가 없으면 SKIP입니다. FAIL이 하나라도 있으면 종료 코드 1, 없으면 0이고, 폴더를 읽을 수 없으면 2입니다.

기준은 촬영에서 얻습니다. 목표 장 수는 사이드카의 frames.target, fps는 매니페스트가 가리키는 촬영 선언(specs/)이나
사이드카의 format.frame_rate, 둘 다 없으면 간격 중앙값입니다. 영상(.mkv)과 열화상 원시값(.raw16)은 열지 않고 크기만
보며, .npy는 머리글만 읽습니다. 화면에서 보는 1절은 이 도구가 보지 않습니다.
"""
import argparse
import hashlib
import json
import os
import re
import sys
import unicodedata
from dataclasses import asdict, dataclass
from typing import Callable, Dict, List, Optional, Tuple

PASS, FAIL, WARN, SKIP = "PASS", "FAIL", "WARN", "SKIP"
CODE, DECIDED, UNDECIDED = "코드", "결정", "미정"

SPECS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "specs")

# 앱이 녹화를 시작할 때 쓰는 간격 허용 오차(platform/src/capture.cc)와 같은 값을 다른 fps 비교에도 씁니다.
TOLERANCE_PERCENT = 5
# 간격이 주기의 1.5배를 넘으면 누락입니다(core/src/timing/frame_gap.cc).
GAP_FACTOR = 1.5
# RGB 녹화 구간 안 열화상 장 수의 최솟값입니다(core/src/record/include/hwlab/record/analysis_inputs.h).
MIN_THERMAL_FRAMES_IN_RGB_WINDOW = 225
CLOCK_RATE_TOLERANCE = 0.001

S2 = "2. RGB 발 영상 사이드카"
S3 = "3. 프레임 시각"
S4 = "4. 열화상"
S5 = "5. PPG"
S6 = "6. 장치 사이 시각"
S7 = "7. 카메라와 조명 설정"
S8 = "8. 매니페스트"

FEET = (("left", "왼발"), ("right", "오른발"))


class Skip(Exception):
    """판정할 자료가 없습니다. 메시지가 이유입니다."""


class Unmet(Exception):
    """있어야 하는 파일이나 값이 없거나 읽히지 않습니다. 메시지가 현재 상태입니다."""


@dataclass(frozen=True)
class Verdict:
    ok: bool
    value: str
    expected: str


@dataclass(frozen=True)
class Result:
    section: str
    name: str
    status: str
    value: str
    expected: str
    source: str


@dataclass(frozen=True)
class Check:
    section: str
    name: str
    source: str
    run: Callable[["Shot"], Verdict]


CHECKS: List[Check] = []


class Doc:
    """JSON 파일 하나입니다. 파일이 없거나 깨졌으면 data가 None이고 problem이 이유입니다."""

    def __init__(self, filename: str, data: Optional[dict], problem: str):
        self.filename = filename
        self.data = data
        self.problem = problem

    def require(self) -> "Doc":
        if self.data is None:
            raise Unmet(self.problem)
        return self

    def optional(self) -> "Doc":
        if self.data is None:
            raise Skip(self.problem)
        return self

    def get(self, path: str):
        node = self.data
        for key in path.split("."):
            if not isinstance(node, dict) or key not in node:
                raise Skip("%s 없음(schema %s)" % (path, (self.data or {}).get("schema_version", "?")))
            node = node[key]
        if node is None:
            raise Skip("%s가 null(값을 알 수 없음)" % path)
        return node


def read_doc(folder: str, filename: str) -> Doc:
    path = os.path.join(folder, filename)
    if not os.path.isfile(path):
        return Doc(filename, None, "%s 없음" % filename)
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return Doc(filename, None, "%s를 읽지 못함(JSON 오류)" % filename)
    if not isinstance(data, dict):
        return Doc(filename, None, "%s를 읽지 못함(JSON 객체가 아님)" % filename)
    return Doc(filename, data, "")


def find_prefix(folder: str) -> Optional[str]:
    names = sorted(os.listdir(folder))
    for suffix in ("_shot.json", "_left.json", "_right.json"):
        for name in names:
            if name.endswith(suffix):
                return name[: -len(suffix)]
    return None


class Shot:
    """촬영 폴더의 파일을 읽어 둔 것입니다. 큰 파일은 크기나 머리글만 봅니다."""

    def __init__(self, folder: str, prefix: str):
        self.folder = folder
        self.prefix = prefix
        self.manifest = read_doc(folder, prefix + "_shot.json")
        self.docs: Dict[str, Doc] = {
            "left": read_doc(folder, prefix + "_left.json"),
            "right": read_doc(folder, prefix + "_right.json"),
            "thermal": read_doc(folder, prefix + "_thermal.json"),
            "ppg": read_doc(folder, prefix + "_ppg.json"),
        }
        self._spec: Optional[Tuple[str, bytes]] = None
        self._ppg_csv: Optional[Tuple[int, int, int]] = None

    def path(self, suffix: str) -> str:
        return os.path.join(self.folder, self.prefix + suffix)

    def thermal_doc(self) -> Doc:
        doc = self.docs["thermal"]
        if doc.data is not None:
            return doc
        if self.manifest.data is not None and self.manifest.data.get("thermal_recorded") is True:
            raise Unmet(doc.problem)
        raise Skip("열화상을 녹화하지 않은 촬영")

    def ppg_doc(self) -> Doc:
        doc = self.docs["ppg"]
        if doc.data is not None:
            return doc
        if os.path.isfile(self.path("_ppg.csv")):
            raise Unmet(doc.problem)
        raise Skip("PPG 파일 없음(PPG를 열지 않은 촬영)")

    def spec_file(self) -> Tuple[str, bytes]:
        """매니페스트가 가리키는 촬영 선언을 이 레포의 specs/에서 읽습니다."""
        if self._spec is None:
            spec = self.manifest.optional().data.get("spec")
            if not isinstance(spec, dict) or not isinstance(spec.get("file"), str):
                raise Skip("매니페스트에 촬영 선언(spec)이 없음(schema %s)" % self.manifest.data.get("schema_version", "?"))
            name = os.path.basename(spec["file"])
            path = os.path.join(SPECS_DIR, name)
            if not os.path.isfile(path):
                raise Skip("specs/에 %s 없음" % name)
            with open(path, "rb") as f:
                self._spec = (name, f.read())
        return self._spec

    def spec_setting(self, key: str):
        name, raw = self.spec_file()
        try:
            settings = json.loads(raw.decode("utf-8"))["settings"]
        except (ValueError, KeyError):
            raise Skip("촬영 선언 %s를 읽지 못함" % name)
        if key not in settings:
            raise Skip("촬영 선언 %s에 settings.%s 없음" % (name, key))
        return settings[key]

    def period_us(self, side: Doc) -> Tuple[float, str]:
        """한 프레임 주기와 그 출처입니다. 출처가 간격 중앙값이면 기준으로 삼을 수 없습니다."""
        try:
            return 1_000_000 / self.spec_setting("sdk_frame_rate"), "촬영 선언"
        except Skip:
            pass
        try:
            return 1_000_000 / side.get("format.frame_rate"), "사이드카"
        except Skip:
            return float(side.get("frames.median_interval_us")), "중앙값"

    def ppg_csv(self) -> Tuple[int, int, int]:
        """표본 줄 수, 첫 공통 시각, 마지막 공통 시각입니다. 머리글 줄은 세지 않습니다."""
        if self._ppg_csv is None:
            self.ppg_doc()
            path = self.path("_ppg.csv")
            if not os.path.isfile(path):
                raise Unmet(os.path.basename(path) + " 없음")
            rows, first, last = 0, 0, 0
            with open(path, "r", encoding="utf-8") as f:
                next(f, None)
                for line in f:
                    if not line.strip():
                        continue
                    last = int(line.split(",", 1)[0])
                    first = first if rows else last
                    rows += 1
            if rows == 0:
                raise Unmet("PPG 표본이 없음")
            self._ppg_csv = (rows, first, last)
        return self._ppg_csv


def foot_sidecar(shot: Shot, foot: str) -> Doc:
    return shot.docs[foot].require()


def shot_check(section: str, name: str, source: str):
    """발과 상관없이 촬영 하나에 한 번 판정하는 항목을 등록합니다."""

    def register(fn: Callable[[Shot], Verdict]):
        CHECKS.append(Check(section, name, source, fn))
        return fn

    return register


def foot_check(section: str, name: str, source: str):
    """왼발과 오른발 사이드카를 각각 판정하고 한 항목으로 합칩니다."""

    def register(fn: Callable[[Shot, Doc], Verdict]):
        CHECKS.append(Check(section, name, source, lambda shot: both_feet(shot, fn)))
        return fn

    return register


def both_feet(shot: Shot, fn: Callable[[Shot, Doc], Verdict]) -> Verdict:
    parts: List[Tuple[str, Verdict]] = []
    skips: List[str] = []
    for foot, label in FEET:
        try:
            parts.append((label, fn(shot, foot_sidecar(shot, foot))))
        except Skip as why:
            skips.append(str(why))
        except Unmet as why:
            parts.append((label, Verdict(False, str(why), "")))
    if not parts:
        raise Skip(skips[0])
    expected = next((v.expected for _, v in parts if v.expected), "")
    values = {v.value for _, v in parts}
    if len(parts) == len(FEET) and len(values) == 1:
        value = "두 발 " + values.pop()
    else:
        value = ", ".join("%s %s" % (label, v.value) for label, v in parts)
    return Verdict(all(v.ok for _, v in parts), value, expected)


def intervals(values: List[int]) -> List[int]:
    return [b - a for a, b in zip(values, values[1:])]


def count(n: int, unit: str) -> str:
    return "%s%s" % (format(n, ","), unit)


def gap_detail(gaps: List[dict]) -> str:
    shown = ", ".join("프레임 %s 뒤 %s장" % (g["after_frame"], g["missing"]) for g in gaps[:3])
    return shown + (" 외" if len(gaps) > 3 else "")


def rgb_window(shot: Shot) -> Tuple[int, int]:
    """읽을 수 있는 RGB 사이드카의 첫 공통 시각과 마지막 공통 시각입니다."""
    firsts, lasts = [], []
    for foot, _ in FEET:
        doc = shot.docs[foot]
        if doc.data is not None:
            times = doc.get("per_frame.common_time_us")
            firsts.append(times[0])
            lasts.append(times[-1])
    if not firsts:
        raise Skip("RGB 사이드카가 없어 녹화 구간을 모름")
    return min(firsts), max(lasts)


def npy_shape(path: str) -> Tuple[int, ...]:
    """NumPy 파일의 머리글만 읽어 모양을 돌려줍니다."""
    with open(path, "rb") as f:
        if f.read(6) != b"\x93NUMPY":
            raise Unmet("NumPy 파일이 아님")
        major = f.read(2)[0]
        header_len = int.from_bytes(f.read(2 if major == 1 else 4), "little")
        header = f.read(min(header_len, 1 << 20)).decode("latin-1")
    found = re.search(r"'shape'\s*:\s*\(([^)]*)\)", header)
    if not found:
        raise Unmet("NumPy 머리글에 shape 없음")
    return tuple(int(part) for part in found.group(1).split(",") if part.strip())


def csv_rows(path: str) -> int:
    with open(path, "r", encoding="utf-8") as f:
        next(f, None)
        return sum(1 for line in f if line.strip())


@foot_check(S2, "장 수", DECIDED)
def rgb_frames_written(shot: Shot, side: Doc) -> Verdict:
    written, target = side.get("frames.written"), side.get("frames.target")
    return Verdict(written == target, count(written, "장"), "목표 " + count(target, "장"))


@shot_check(S2, "좌우 장 수", DECIDED)
def rgb_feet_same_count(shot: Shot) -> Verdict:
    left, right = (foot_sidecar(shot, foot).get("frames.written") for foot, _ in FEET)
    return Verdict(left == right, "왼발 %s, 오른발 %s" % (count(left, "장"), count(right, "장")), "같음")


@foot_check(S2, "카메라 단 누락", DECIDED)
def rgb_missing(shot: Shot, side: Doc) -> Verdict:
    missing = side.get("frames.missing")
    detail = "(%s)" % gap_detail(side.get("frames.gaps")) if missing else ""
    return Verdict(missing == 0, count(missing, "장") + detail, "0장")


@foot_check(S2, "저장 누락", DECIDED)
def rgb_dropped(shot: Shot, side: Doc) -> Verdict:
    dropped = side.get("frames.dropped")
    return Verdict(dropped == 0, count(dropped, "장"), "0장")


@foot_check(S2, "간격 중앙값", CODE)
def rgb_median_interval(shot: Shot, side: Doc) -> Verdict:
    period, origin = shot.period_us(side)
    if origin == "중앙값":
        raise Skip("선언한 fps를 알 수 없어 견줄 기준이 없음")
    median = side.get("frames.median_interval_us")
    ok = abs(median - period) * 100 <= period * TOLERANCE_PERCENT
    return Verdict(ok, "%s us" % format(median, ","), "%s us ±%d%%" % (format(round(period), ","), TOLERANCE_PERCENT))


@foot_check(S2, "느린 간격", CODE)
def rgb_slow_intervals(shot: Shot, side: Doc) -> Verdict:
    slow = side.get("frames.slow_intervals")
    return Verdict(slow == 0, count(slow, "곳"), "0곳")


@foot_check(S2, "1초 최저 fps", UNDECIDED)
def rgb_min_fps(shot: Shot, side: Doc) -> Verdict:
    period, _ = shot.period_us(side)
    floor = 1_000_000 / period * (100 - TOLERANCE_PERCENT) / 100
    lowest = side.get("frames.min_fps_1s")
    return Verdict(lowest >= floor, "%.1f fps" % lowest, "%.1f fps 이상(선언의 %d%%)" % (floor, 100 - TOLERANCE_PERCENT))


@foot_check(S2, "쓰기 대기열", CODE)
def rgb_queue(shot: Shot, side: Doc) -> Verdict:
    peak, limit = side.get("frames.max_queue_frames"), side.get("frames.buffer_frames")
    return Verdict(peak < limit, count(peak, "장"), count(limit, "장") + " 미만")


@foot_check(S2, "녹화 길이", DECIDED)
def rgb_duration(shot: Shot, side: Doc) -> Verdict:
    period, _ = shot.period_us(side)
    seconds = (side.get("frames.last_time_us") - side.get("frames.first_time_us")) / 1e6
    want = side.get("frames.target") * period / 1e6
    return Verdict(abs(seconds - want) * 100 <= want * TOLERANCE_PERCENT, "%.2f초" % seconds, "%.1f초 ±%d%%" % (want, TOLERANCE_PERCENT))


@foot_check(S3, "번호 연속", UNDECIDED)
def frame_index_consecutive(shot: Shot, side: Doc) -> Verdict:
    index = side.get("per_frame.index")
    breaks = sum(1 for step in intervals(index) if step != 1)
    return Verdict(breaks == 0, "%s(시작 %d)" % ("연속" if breaks == 0 else "%d곳 끊김" % breaks, index[0]), "1씩 증가")


@foot_check(S3, "시각 증가", UNDECIDED)
def common_time_increases(shot: Shot, side: Doc) -> Verdict:
    steps = intervals(side.get("per_frame.common_time_us"))
    stuck = [i for i, step in enumerate(steps) if step <= 0]
    value = "모두 증가" if not stuck else "%d곳에서 증가하지 않음(프레임 %d 다음 등)" % (len(stuck), stuck[0])
    return Verdict(not stuck, value, "모든 간격이 0보다 큼")


@foot_check(S3, "카메라 시각과 수신 시각", UNDECIDED)
def device_and_host_gaps_agree(shot: Shot, side: Doc) -> Verdict:
    limit = shot.period_us(side)[0] * GAP_FACTOR
    wide_device = {i for i, step in enumerate(intervals(side.get("per_frame.device_time_us"))) if step > limit}
    wide_host = {i for i, step in enumerate(intervals(side.get("per_frame.host_time_us"))) if step > limit}
    apart = wide_device ^ wide_host
    value = "어긋난 간격 %d곳(함께 벌어진 간격 %d곳)" % (len(apart), len(wide_device & wide_host))
    return Verdict(not apart, value, "어긋난 간격 0곳")


@foot_check(S3, "시계 맞춤 잔차", UNDECIDED)
def clock_residual(shot: Shot, side: Doc) -> Verdict:
    return Verdict(True, "%s us" % format(side.get("clock.max_residual_us"), ","), "기록만 함")


@foot_check(S3, "시계 속도", UNDECIDED)
def clock_rate(shot: Shot, side: Doc) -> Verdict:
    rate = side.get("clock.rate")
    return Verdict(abs(rate - 1) <= CLOCK_RATE_TOLERANCE, "%.9f" % rate, "1 ±%g" % CLOCK_RATE_TOLERANCE)


@shot_check(S4, "RGB 구간 안 장 수", CODE)
def thermal_in_window(shot: Shot) -> Verdict:
    n = shot.thermal_doc().get("frames.written_in_rgb_window")
    return Verdict(n >= MIN_THERMAL_FRAMES_IN_RGB_WINDOW, count(n, "장"), "%d장 이상" % MIN_THERMAL_FRAMES_IN_RGB_WINDOW)


@shot_check(S4, "열화상 누락", CODE)
def thermal_missing(shot: Shot) -> Verdict:
    doc = shot.thermal_doc()
    missing, dropped = doc.get("frames.missing"), doc.get("frames.dropped")
    return Verdict(missing + dropped == 0, "카메라 단 %d장, 저장 %d장" % (missing, dropped), "0장")


@shot_check(S4, "열화상 파일 크기", UNDECIDED)
def thermal_raw_size(shot: Shot) -> Verdict:
    doc = shot.thermal_doc()
    frame_bytes = doc.get("format.width") * doc.get("format.height") * 2
    path = shot.path("_thermal.raw16")
    if not os.path.isfile(path):
        raise Unmet(os.path.basename(path) + " 없음")
    frames, rest = divmod(os.path.getsize(path), frame_bytes)
    written = doc.get("frames.written")
    value = count(frames, "장") + ("(나머지 %d바이트)" % rest if rest else "")
    return Verdict(frames == written and rest == 0, value, "frames.written " + count(written, "장"))


@shot_check(S4, "발 영역 파일", UNDECIDED)
def thermal_arrays(shot: Shot) -> Verdict:
    written = shot.thermal_doc().get("frames.written")
    shapes, absent = [], []
    for name in ("thermal_left.npy", "thermal_right.npy"):
        path = os.path.join(shot.folder, name)
        if os.path.isfile(path):
            shapes.append(npy_shape(path)[0])
        else:
            absent.append(name)
    if absent:
        return Verdict(False, "%s 없음" % ", ".join(absent), "둘 다 있음")
    return Verdict(all(first == written for first in shapes), "첫 차원 %s" % ", ".join(str(s) for s in shapes), "첫 차원 %d" % written)


@shot_check(S4, "카메라 온도 기록", UNDECIDED)
def thermal_camera_temp(shot: Shot) -> Verdict:
    path = shot.path("_thermal_camera_temp.csv")
    if not os.path.isfile(path):
        raise Skip("%s 없음(카메라 온도를 주는 장비만 만듦)" % os.path.basename(path))
    written = shot.thermal_doc().get("frames.written")
    rows = csv_rows(path)
    return Verdict(rows == written, count(rows, "줄"), "frames.written " + count(written, "장"))


@shot_check(S5, "표본 누락", UNDECIDED)
def ppg_missing(shot: Shot) -> Verdict:
    doc = shot.ppg_doc()
    missing, gaps = doc.get("samples.missing"), doc.get("samples.gaps")
    return Verdict(missing == 0 and not gaps, "%s, 간격 %d곳" % (count(missing, "개"), len(gaps)), "0개, 간격 0곳")


@shot_check(S5, "표본 간격", UNDECIDED)
def ppg_interval(shot: Shot) -> Verdict:
    doc = shot.ppg_doc()
    period = 1_000_000 / doc.get("sample_rate_hz")
    median = doc.get("samples.median_interval_us")
    ok = abs(median - period) * 100 <= period * TOLERANCE_PERCENT
    return Verdict(ok, "%s us" % format(median, ","), "%s us ±%d%%" % (format(round(period), ","), TOLERANCE_PERCENT))


@shot_check(S5, "RGB 구간 안 측정 오류", UNDECIDED)
def ppg_measure_errors(shot: Shot) -> Verdict:
    codes = shot.ppg_doc().get("measure_errors.rgb_window.codes")
    bad = sum(c["samples"] for c in codes)
    detail = "(%s)" % ", ".join("%s %d개" % (c["code"], c["samples"]) for c in codes) if codes else ""
    return Verdict(bad == 0, count(bad, "개") + detail, "0개")


@shot_check(S5, "통신 오류", UNDECIDED)
def ppg_link(shot: Shot) -> Verdict:
    doc = shot.ppg_doc()
    names = ("crc_failures", "bad_lengths", "implausible", "skipped_bytes")
    counts = [doc.get("link." + name) for name in names]
    return Verdict(not any(counts), ", ".join("%s %d" % pair for pair in zip(names, counts)), "모두 0")


@shot_check(S5, "끊김(장치 오류 메시지)", UNDECIDED)
def ppg_events(shot: Shot) -> Verdict:
    errors = [e for e in shot.ppg_doc().get("events") if e.get("code") == "e"]
    return Verdict(not errors, count(len(errors), "건"), "장치 오류 메시지 0건")


@shot_check(S5, "CSV 줄 수", UNDECIDED)
def ppg_csv_rows(shot: Shot) -> Verdict:
    rows = shot.ppg_csv()[0]
    samples = shot.ppg_doc().get("samples.count")
    return Verdict(rows == samples, count(rows, "줄"), "samples.count " + count(samples, "개"))


@shot_check(S6, "두 발 첫 프레임 시각 차이", DECIDED)
def feet_start_together(shot: Shot) -> Verdict:
    left, right = (foot_sidecar(shot, foot) for foot, _ in FEET)
    period, _ = shot.period_us(left)
    diff = abs(left.get("per_frame.common_time_us")[0] - right.get("per_frame.common_time_us")[0])
    return Verdict(diff <= period, "%s us" % format(diff, ","), "%s us 이하(한 프레임 주기)" % format(round(period), ","))


@shot_check(S6, "열화상이 RGB 구간을 덮음", UNDECIDED)
def thermal_covers_rgb(shot: Shot) -> Verdict:
    begin, end = rgb_window(shot)
    doc = shot.thermal_doc()
    times = doc.get("per_frame.common_time_us")
    slack = doc.get("frames.record_period_us") or doc.get("frames.median_interval_us")
    late, early = times[0] - begin, end - times[-1]
    value = "시작 %s us, 끝 %s us" % (format(late, "+,"), format(-early, "+,"))
    expected = "시작 +%s us 이하, 끝 -%s us 이상(열화상 기록 주기)" % (format(slack, ","), format(slack, ","))
    return Verdict(late <= slack and early <= slack, value, expected)


@shot_check(S6, "PPG가 RGB 구간을 덮음", UNDECIDED)
def ppg_covers_rgb(shot: Shot) -> Verdict:
    begin, end = rgb_window(shot)
    _, first, last = shot.ppg_csv()
    before, after = (begin - first) / 1e6, (last - end) / 1e6
    return Verdict(before >= 0 and after >= 0, "앞 %+.2f초, 뒤 %+.2f초" % (before, after), "앞뒤 모두 0초 이상")


def equals_declaration(shot: Shot, side: Doc, key: str) -> Verdict:
    want, got = shot.spec_setting(key), side.get("settings." + key)
    return Verdict(got == want, str(got), str(want))


@foot_check(S7, "노출 방식", DECIDED)
def setting_mode(shot: Shot, side: Doc) -> Verdict:
    mode = side.get("settings.mode")
    return Verdict(mode == "manual", mode, "manual")


@foot_check(S7, "노출", DECIDED)
def setting_exposure(shot: Shot, side: Doc) -> Verdict:
    return equals_declaration(shot, side, "exposure_step")


@foot_check(S7, "게인", DECIDED)
def setting_gain(shot: Shot, side: Doc) -> Verdict:
    return equals_declaration(shot, side, "gain")


@foot_check(S7, "화이트 밸런스", DECIDED)
def setting_white_balance(shot: Shot, side: Doc) -> Verdict:
    return equals_declaration(shot, side, "white_balance_k")


@foot_check(S7, "설정 고정", DECIDED)
def setting_locked(shot: Shot, side: Doc) -> Verdict:
    locked = side.get("settings.locked")
    return Verdict(locked is True, str(locked).lower(), "true")


@foot_check(S7, "조명 적용", UNDECIDED)
def light_applied(shot: Shot, side: Doc) -> Verdict:
    source = side.get("light.source")
    if source != "app":
        raise Skip("앱이 조명을 제어하지 않음(light.source %s)" % source)
    response = side.get("light.response")
    return Verdict(response == "applied", response, "applied")


@foot_check(S7, "깜빡임", UNDECIDED)
def light_flicker(shot: Shot, side: Doc) -> Verdict:
    return Verdict(True, "periodic_fraction %s" % side.get("light.flicker.periodic_fraction"), "기록만 함")


@shot_check(S8, "files에 적힌 파일이 모두 있음", DECIDED)
def manifest_files_exist(shot: Shot) -> Verdict:
    names = sorted(set(shot.manifest.require().get("files").values()))
    absent = [name for name in names if not os.path.isfile(os.path.join(shot.folder, name))]
    value = "%d개 중 %d개 없음" % (len(names), len(absent)) + (": " + ", ".join(absent[:5]) if absent else "")
    return Verdict(not absent, value, "모두 있음")


@shot_check(S8, "촬영 선언 SHA-256", DECIDED)
def manifest_spec_sha(shot: Shot) -> Verdict:
    actual = hashlib.sha256(shot.spec_file()[1]).hexdigest()
    recorded = shot.manifest.get("spec.sha256")
    return Verdict(recorded == actual, "매니페스트 %s, 파일 %s" % (recorded[:12], actual[:12]), "같음")


def run_checks(folder: str) -> List[Result]:
    prefix = find_prefix(folder)
    if prefix is None:
        raise Unmet("촬영 폴더가 아님(*_shot.json, *_left.json, *_right.json이 없음)")
    shot = Shot(folder, prefix)
    results = []
    for check in CHECKS:
        try:
            verdict = check.run(shot)
        except Skip as why:
            results.append(Result(check.section, check.name, SKIP, str(why), "", check.source))
            continue
        except Unmet as why:
            verdict = Verdict(False, str(why), "")
        except (TypeError, ValueError, KeyError, IndexError, ZeroDivisionError) as why:
            verdict = Verdict(False, "값을 읽지 못함(%s)" % type(why).__name__, "")
        status = PASS if verdict.ok else (WARN if check.source == UNDECIDED else FAIL)
        results.append(Result(check.section, check.name, status, verdict.value, verdict.expected, check.source))
    return results


def display_width(text: str) -> int:
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in text)


def format_report(results: List[Result]) -> str:
    width = max(display_width(r.name) for r in results)
    lines, section = [], None
    for r in results:
        if r.section != section:
            section = r.section
            lines.append("")
            lines.append(section)
        name = r.name + " " * (width - display_width(r.name))
        if r.status == SKIP:
            tail = r.value
        else:
            tail = "%s  (%s%s)" % (r.value, "기준 %s, " % r.expected if r.expected else "", r.source)
        lines.append("  %-4s  %s  %s" % (r.status, name, tail))
    totals = ", ".join("%s %d" % (s, sum(1 for r in results if r.status == s)) for s in (FAIL, WARN, PASS, SKIP))
    lines.append("")
    lines.append("결과: " + totals)
    return "\n".join(lines).lstrip("\n")


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="촬영 폴더 하나를 녹화 확인 체크리스트로 판정합니다.")
    parser.add_argument("folder", help="촬영 폴더(<날짜_시각>_shot.json이 있는 폴더)")
    parser.add_argument("--json", action="store_true", help="항목별 결과를 JSON 목록으로 냅니다")
    args = parser.parse_args(argv)
    if not os.path.isdir(args.folder):
        print("폴더가 아닙니다: %s" % args.folder, file=sys.stderr)
        return 2
    try:
        results = run_checks(args.folder)
    except Unmet as why:
        print(str(why), file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps([asdict(r) for r in results], ensure_ascii=False, indent=2))
    else:
        print(format_report(results))
    return 1 if any(r.status == FAIL for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
