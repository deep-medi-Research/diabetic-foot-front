#!/usr/bin/env python3
"""check_shot.py 시험입니다. 작은 가짜 촬영 폴더를 임시 폴더에 만들어 항목별 판정을 확인합니다.

    python3 tools/test_check_shot.py
"""
import contextlib
import hashlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_shot  # noqa: E402

SPECS = check_shot.SPECS_DIR
STAMP = "20261105_164616"
THERMAL_FRAMES = 270
THERMAL_PERIOD_US = 111111
THERMAL_SHAPE = (4, 3)
START_US = 1_000_000_000_000


def spec_for(fps: int) -> str:
    return {60: "see3cam_manual_720p60.json", 30: "see3cam_manual_30fps.json"}[fps]


def write_json(folder: str, name: str, data: dict) -> None:
    with open(os.path.join(folder, name), "w", encoding="utf-8") as f:
        json.dump(data, f)


def write_npy(path: str, shape: tuple) -> None:
    header = "{'descr': '<u2', 'fortran_order': False, 'shape': %r, }" % (shape,)
    header += " " * (63 - (10 + len(header)) % 64) + "\n"
    with open(path, "wb") as f:
        f.write(b"\x93NUMPY\x01\x00" + struct.pack("<H", len(header)) + header.encode("latin-1"))
        f.write(bytes(shape[0] * shape[1] * shape[2] * 2))


def rgb_sidecar(spec: dict, foot: str, fps: int) -> dict:
    period = round(1_000_000 / fps)
    target = spec["target_frames"]
    device = [START_US + i * period for i in range(target)]
    return {
        "schema_version": 8,
        "stream": "rgb",
        "foot": foot,
        "light": {"source": "app", "response": "applied", "flicker": {"periodic_fraction": 0.1}},
        "format": {"pixel_format": "UYVY", "width": 640, "height": 720, "frame_rate": fps, "codec": "ffv1"},
        "settings": {
            "mode": spec["settings"]["mode"],
            "exposure_step": spec["settings"]["exposure_step"],
            "gain": spec["settings"]["gain"],
            "white_balance_k": spec["settings"]["white_balance_k"],
            "sdk_frame_rate": fps,
            "locked": True,
        },
        "clock": {"offset_us": 123, "rate": 1.0, "max_residual_us": 400},
        "frames": {
            "target": target,
            "written": target,
            "received": target,
            "first_time_us": device[0],
            "last_time_us": device[-1],
            "median_interval_us": period,
            "missing": 0,
            "dropped": 0,
            "buffer_frames": spec["buffer_frames"],
            "max_queue_frames": 3,
            "gaps": [],
            "slow_intervals": 0,
            "min_fps_1s": float(fps),
        },
        "per_frame": {
            "index": [4092 + i for i in range(target)],
            "device_time_us": device,
            "host_time_us": [t + 5000 for t in device],
            "common_time_us": [t + 123 for t in device],
        },
    }


def thermal_sidecar(begin: int, end: int) -> dict:
    times = [begin + 50_000 + i * THERMAL_PERIOD_US for i in range(THERMAL_FRAMES)]
    return {
        "schema_version": 8,
        "stream": "thermal",
        "format": {"pixel_format": "Y16", "width": THERMAL_SHAPE[0], "height": THERMAL_SHAPE[1], "frame_rate": 9, "codec": "raw16"},
        "frames": {
            "written": THERMAL_FRAMES,
            "written_in_rgb_window": THERMAL_FRAMES,
            "record_period_us": THERMAL_PERIOD_US,
            "median_interval_us": THERMAL_PERIOD_US,
            "missing": 0,
            "dropped": 0,
        },
        "per_frame": {"common_time_us": times},
    }


def ppg_files(folder: str, begin: int, end: int) -> dict:
    times = list(range(begin - 500_000, end + 500_000, 2000))
    with open(os.path.join(folder, STAMP + "_ppg.csv"), "w", encoding="utf-8") as f:
        f.write("common_time_us,value,measure_error,heart_rate,spo2\n")
        for t in times:
            f.write("%d,50000,0,60,98\n" % t)
    return {
        "schema_version": 2,
        "sample_rate_hz": 500,
        "samples": {"count": len(times), "median_interval_us": 2000, "missing": 0, "gaps": []},
        "measure_errors": {"rgb_window": {"samples": 1000, "codes": []}},
        "link": {"crc_failures": 0, "bad_lengths": 0, "implausible": 0, "skipped_bytes": 0},
        "events": [{"time_us": begin, "code": "s", "value": "v"}],
    }


def build_shot(folder: str, fps: int = 60, with_ppg: bool = True, mutate=None) -> str:
    """깨끗한 촬영 폴더를 만들고 mutate(docs)로 사이드카를 바꿀 수 있게 합니다."""
    spec_name = spec_for(fps)
    with open(os.path.join(SPECS, spec_name), "rb") as f:
        raw = f.read()
    spec = json.loads(raw)
    left, right = rgb_sidecar(spec, "left", fps), rgb_sidecar(spec, "right", fps)
    begin, end = left["per_frame"]["common_time_us"][0], left["per_frame"]["common_time_us"][-1]
    docs = {
        "left": left,
        "right": right,
        "thermal": thermal_sidecar(begin, end),
        "manifest": {
            "schema_version": 7,
            "spec": {"file": spec_name, "sha256": hashlib.sha256(raw).hexdigest()},
            "thermal_recorded": True,
            "files": {
                "left_video": STAMP + "_left.mkv",
                "left_sidecar": STAMP + "_left.json",
                "right_video": STAMP + "_right.mkv",
                "right_sidecar": STAMP + "_right.json",
                "thermal_raw": STAMP + "_thermal.raw16",
                "thermal_sidecar": STAMP + "_thermal.json",
                "thermal_left_array": "thermal_left.npy",
                "thermal_right_array": "thermal_right.npy",
                "manifest": STAMP + "_shot.json",
            },
        },
    }
    if with_ppg:
        docs["ppg"] = ppg_files(folder, begin, end)
    if mutate:
        mutate(docs)
    for key, name in (("left", "_left.json"), ("right", "_right.json"), ("thermal", "_thermal.json"), ("manifest", "_shot.json"), ("ppg", "_ppg.json")):
        if key in docs:
            write_json(folder, STAMP + name, docs[key])
    for name in ("_left.mkv", "_right.mkv"):
        with open(os.path.join(folder, STAMP + name), "wb") as f:
            f.write(b"\0")
    with open(os.path.join(folder, STAMP + "_thermal.raw16"), "wb") as f:
        f.write(bytes(THERMAL_FRAMES * THERMAL_SHAPE[0] * THERMAL_SHAPE[1] * 2))
    for name in ("thermal_left.npy", "thermal_right.npy"):
        write_npy(os.path.join(folder, name), (THERMAL_FRAMES, 3, 2))
    with open(os.path.join(folder, STAMP + "_thermal_camera_temp.csv"), "w", encoding="utf-8") as f:
        f.write("index,device_time_us,host_time_us,fpa,shutter_raw,shutter_c\n" + "0,0,0,,,\n" * THERMAL_FRAMES)
    return folder


class CheckShotTest(unittest.TestCase):
    def check(self, **kwargs):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        folder = build_shot(tmp.name, **kwargs)
        return folder, check_shot.run_checks(folder)

    def status(self, results, name):
        found = [r.status for r in results if r.name == name]
        self.assertEqual(len(found), 1, "항목 이름이 하나여야 함: " + name)
        return found[0]

    def statuses(self, results):
        return {r.status for r in results}

    def exit_code(self, folder):
        with contextlib.redirect_stdout(io.StringIO()):
            return check_shot.main([folder])

    def test_clean_shot_passes_every_check(self):
        folder, results = self.check()
        self.assertEqual(self.statuses(results), {check_shot.PASS})
        self.assertEqual(self.exit_code(folder), 0)

    def test_clean_30fps_shot_passes_without_60fps_assumption(self):
        folder, results = self.check(fps=30)
        self.assertEqual(self.statuses(results), {check_shot.PASS})
        self.assertEqual(self.exit_code(folder), 0)

    def test_camera_missing_frames_fail_and_set_exit_code(self):
        def lose_frames(docs):
            docs["right"]["frames"]["missing"] = 6
            docs["right"]["frames"]["gaps"] = [{"after_frame": 100, "interval_us": 116667, "missing": 6}]

        folder, results = self.check(mutate=lose_frames)
        self.assertEqual(self.status(results, "카메라 단 누락"), check_shot.FAIL)
        self.assertEqual(self.status(results, "저장 누락"), check_shot.PASS)
        self.assertEqual(self.exit_code(folder), 1)

    def test_non_increasing_common_time_warns_without_failing(self):
        def stall_clock(docs):
            times = docs["left"]["per_frame"]["common_time_us"]
            times[10] = times[9]

        folder, results = self.check(mutate=stall_clock)
        self.assertEqual(self.status(results, "시각 증가"), check_shot.WARN)
        self.assertNotIn(check_shot.FAIL, self.statuses(results))
        self.assertEqual(self.exit_code(folder), 0)

    def test_shot_without_ppg_skips_ppg_checks(self):
        folder, results = self.check(with_ppg=False)
        for name in ("표본 누락", "표본 간격", "RGB 구간 안 측정 오류", "통신 오류", "끊김(장치 오류 메시지)", "CSV 줄 수", "PPG가 RGB 구간을 덮음"):
            self.assertEqual(self.status(results, name), check_shot.SKIP, name)
        self.assertNotIn(check_shot.FAIL, self.statuses(results))
        self.assertNotIn(check_shot.WARN, self.statuses(results))
        self.assertEqual(self.exit_code(folder), 0)

    def test_fields_missing_in_older_schema_are_skipped(self):
        def old_schema(docs):
            for key in ("left", "right"):
                del docs[key]["frames"]["slow_intervals"]
                del docs[key]["frames"]["min_fps_1s"]

        _, results = self.check(mutate=old_schema)
        self.assertEqual(self.status(results, "느린 간격"), check_shot.SKIP)
        self.assertEqual(self.status(results, "1초 최저 fps"), check_shot.SKIP)
        self.assertEqual(self.status(results, "간격 중앙값"), check_shot.PASS)

    def test_settings_that_differ_from_declaration_fail(self):
        def wrong_settings(docs):
            docs["left"]["settings"]["gain"] = 4
            docs["right"]["settings"]["mode"] = "auto"

        _, results = self.check(mutate=wrong_settings)
        self.assertEqual(self.status(results, "게인"), check_shot.FAIL)
        self.assertEqual(self.status(results, "노출 방식"), check_shot.FAIL)
        self.assertEqual(self.status(results, "노출"), check_shot.PASS)

    def test_slow_camera_is_caught_against_declared_fps(self):
        def slow_camera(docs):
            for key in ("left", "right"):
                docs[key]["frames"]["median_interval_us"] = 66667

        _, results = self.check(mutate=slow_camera)
        self.assertEqual(self.status(results, "간격 중앙값"), check_shot.FAIL)

    def test_spec_hash_mismatch_fails(self):
        def other_spec(docs):
            docs["manifest"]["spec"]["sha256"] = "0" * 64

        _, results = self.check(mutate=other_spec)
        self.assertEqual(self.status(results, "촬영 선언 SHA-256"), check_shot.FAIL)

    def test_listed_file_that_is_gone_fails(self):
        def ghost_file(docs):
            docs["manifest"]["files"]["thermal_preview"] = STAMP + "_thermal.avi"

        _, results = self.check(mutate=ghost_file)
        self.assertEqual(self.status(results, "files에 적힌 파일이 모두 있음"), check_shot.FAIL)

    def test_json_output_lists_one_object_per_check(self):
        folder, _ = self.check()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            check_shot.main([folder, "--json"])
        rows = json.loads(out.getvalue())
        self.assertEqual(len(rows), len(check_shot.CHECKS))
        self.assertEqual(set(rows[0]), {"section", "name", "status", "value", "expected", "source"})

    def test_folder_without_shot_files_is_an_input_error(self):
        with tempfile.TemporaryDirectory() as empty:
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(check_shot.main([empty]), 2)


if __name__ == "__main__":
    unittest.main()
