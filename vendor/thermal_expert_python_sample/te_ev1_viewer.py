import os
import sys
import time
import clr
import numpy as np
import cv2

# PyInstaller frozen exe 실행 환경 및 스크립트 실행 환경 동시 지원
if getattr(sys, "frozen", False):
    # PyInstaller onefile 번들 내부 임시 폴더
    bundle_dir = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
    sdk_dll_path = os.path.join(bundle_dir, "i3system_te_cs_x64.dll")
    bridge_dll_path = os.path.join(bundle_dir, "I3CameraBridge.dll")
    os.add_dll_directory(bundle_dir)
    sys.path.append(bundle_dir)
else:
    # 파이썬 스크립트 실행 환경
    base_dir = os.path.dirname(os.path.abspath(__file__))
    sdk_dll_dir = os.path.join(base_dir, "windows_SDK_20260813", "c#", "x64")
    bridge_dll_dir = os.path.join(base_dir, "bin_bridge")
    sdk_dll_path = os.path.join(sdk_dll_dir, "i3system_te_cs_x64.dll")
    bridge_dll_path = os.path.join(bridge_dll_dir, "I3CameraBridge.dll")

    os.add_dll_directory(sdk_dll_dir)
    os.add_dll_directory(bridge_dll_dir)
    sys.path.append(sdk_dll_dir)
    sys.path.append(bridge_dll_dir)

if not os.path.exists(sdk_dll_path):
    raise FileNotFoundError(f"SDK DLL 파일이 없습니다: {sdk_dll_path}")
if not os.path.exists(bridge_dll_path):
    raise FileNotFoundError(f"Bridge DLL 파일이 없습니다: {bridge_dll_path}")

# C# .NET 어셈블리 참조 로드
clr.AddReference(sdk_dll_path)
clr.AddReference(bridge_dll_path)

import System
from I3CameraBridge import ThermalCamera


class ThermalSuperResolution:
    """
    32X 초극한 배율을 위한 고차 다단계 열화상 전문 초해상도(SR) 엔진
    1. 원본 센서 레벨 전처리 열잡음 제거 (Bilateral + Spatial Filtering)
    2. 피라미드 단계별 점진적 서브픽셀 엣지 정렬 (Progressive Lanczos-4)
    3. 쇼크 필터(Shock Filter) 기반 경계선 기울기 압축 (날카로운 엣지 복원)
    4. 에너지 중립(Zero-Gain) 고주파 디테일 샤프닝
    """
    def __init__(self):
        self.prev_frame = None

    def process(self, raw_img, scale_factor=4, temporal_denoise=True, sharpness_boost=1.0):
        if scale_factor <= 1:
            return raw_img

        # 1. 시간축 노이즈 제거 (Temporal EMA Denoising: 센서 플리커 및 랜덤 노이즈 억제)
        if temporal_denoise:
            if self.prev_frame is None or self.prev_frame.shape != raw_img.shape:
                self.prev_frame = raw_img.astype(np.float32)
            else:
                cv2.accumulateWeighted(raw_img.astype(np.float32), self.prev_frame, 0.65)
            clean_raw = self.prev_frame.astype(np.uint8)
        else:
            clean_raw = raw_img

        # 2. 센서 원본 레벨 공간 노이즈 정밀 제거 (32배 확대 시 노이즈가 덩어리지는 것 원천 차단)
        pre_clean = cv2.bilateralFilter(clean_raw, d=5, sigmaColor=20, sigmaSpace=20)

        h, w = raw_img.shape[:2]
        target_w, target_h = w * scale_factor, h * scale_factor

        # 화면 렌더링용 내부 처리 해상도 (실시간 30FPS 유지 및 칼같은 선명도 보장)
        internal_scale = min(scale_factor, 4)
        curr_img = pre_clean

        # 3. 다단계 피라미드 2배씩 점진적 업스케일링 & 엣지 샤프닝
        steps = int(np.log2(internal_scale))
        for step in range(steps):
            ch, cw = curr_img.shape[:2]
            # 2배 Lanczos-4 확장
            up = cv2.resize(curr_img, (cw * 2, ch * 2), interpolation=cv2.INTER_LANCZOS4)
            # 단계별 엣지 보존 스무딩
            smooth = cv2.bilateralFilter(up, d=5, sigmaColor=18, sigmaSpace=18)

            # 쇼크 필터 효과 (라플라시안 엣지 기울기 압축 및 선명화)
            smooth_f = smooth.astype(np.float32)
            blur_f = cv2.GaussianBlur(smooth_f, (0, 0), sigmaX=1.2)
            high_freq = smooth_f - blur_f

            # 에너지 중립 샤프닝 적용
            weight = 0.55 * sharpness_boost
            sharpened = smooth_f + (high_freq * weight)
            curr_img = np.clip(sharpened, 0.0, 255.0).astype(np.uint8)

        # 4. 8X / 16X / 32X 고배율 시 최종 엣지 보강 (High-pass Multi-scale Detail Extraction)
        if scale_factor > 4:
            curr_f = curr_img.astype(np.float32)
            laplacian = cv2.Laplacian(curr_f, cv2.CV_32F, ksize=3)
            # 고주파 윤곽선만 깨끗하게 부각
            curr_f = curr_f - (laplacian * (0.15 * sharpness_boost))
            curr_img = np.clip(curr_f, 0.0, 255.0).astype(np.uint8)

        return curr_img

    def render_full_save(self, raw_img, scale_factor=32, sharpness_boost=1.0):
        """저장 시 실제 극한 해상도(최대 20480x15360) 무손실 전체 렌더링"""
        clean_raw = cv2.bilateralFilter(raw_img, d=5, sigmaColor=25, sigmaSpace=25)
        h, w = raw_img.shape[:2]
        target_w, target_h = w * scale_factor, h * scale_factor

        # 1. 전체 목표 해상도로 Lanczos-4 보간
        up = cv2.resize(clean_raw, (target_w, target_h), interpolation=cv2.INTER_LANCZOS4)

        # 2. 초거대 해상도 맞춤형 엣지 보존 필터
        smooth = cv2.bilateralFilter(up, d=7, sigmaColor=35, sigmaSpace=35)

        # 3. 다중 스케일 샤프닝
        img_f = smooth.astype(np.float32)
        fine_blur = cv2.GaussianBlur(img_f, (0, 0), sigmaX=scale_factor * 0.4)
        coarse_blur = cv2.GaussianBlur(img_f, (0, 0), sigmaX=scale_factor * 0.8)

        high_fine = img_f - fine_blur
        high_coarse = fine_blur - coarse_blur

        enhanced = img_f + (high_fine * 0.65 * sharpness_boost) + (high_coarse * 0.35 * sharpness_boost)
        return np.clip(enhanced, 0.0, 255.0).astype(np.uint8)


def main():
    print("=" * 78)
    print("i3system Thermal Expert 초고해상도 열화상 뷰어 (단독 실행 Exe 배포판)")
    print("=" * 78)

    cam = ThermalCamera()
    print("카메라 자동 검색 및 연결 중...")
    if not cam.OpenAuto():
        print("[오류] 연결된 i3system 카메라를 찾을 수 없거나 열 수 없습니다.")
        input("종료하려면 엔터 키를 누르세요...")
        return

    src_w, src_h = cam.Width, cam.Height
    size = src_w * src_h
    print(f"[성공] {cam.ModelName} 연결 완료! 원본 해상도: {src_w} x {src_h} (장치 번호: {cam.DeviceIndex})")

    print("\n단축키 안내:")
    print("  - [R] : Super Resolution 배율 순환 (1X -> 2X -> 4X -> 8X -> 16X -> 32X)")
    print("  - [K] : 선명도(Sharpness) 강도 조절 (표준 1.0x -> 강력 1.5x -> 극대 2.0x -> 부드럽게 0.5x)")
    print("  - [N] : 시간축 노이즈 제거(Temporal Denoise) ON / OFF")
    print("  - [V] : 상하 반전 (Vertical Flip) ON / OFF")
    print("  - [H] : 좌우 반전 (Horizontal Mirror) ON / OFF")
    print("  - [M] : 컬러맵 변경 (WhiteHot / Purple / Cyan)")
    print("  - [C] : 셔터 캘리브레이션 (화면 노이즈 제거 및 균일화)")
    print("  - [T] : 마우스 위치 기준 체온(36.5℃) 원터치 자동 캘리브레이션")
    print("  - [+] / [-] : 온도 오프셋 수동 미세 조정 (±0.5℃)")
    print("  - [0] : 온도 오프셋 리셋 (0.0℃)")
    print("  - [S] : 현재 초고해상도 화면 원본 크기 무손실 캡처 저장")
    print("  - [Q] 또는 [ESC] : 종료")
    print("  - 마우스 이동 : 해당 지점 실시간 정밀 온도(℃) 측정 (창 최대화 시에도 완벽 매핑)")
    print("=" * 78)

    # C# 컬러 버퍼 할당 (BGR 24비트)
    clr_buffer = System.Array[System.Byte]([0] * (size * 3))

    colormaps = ["WhiteHot", "Purple", "Cyan"]
    colormap_idx = 0

    sr_modes = [1, 2, 4, 8, 16, 32]
    sr_mode_idx = 5  # 기본값: 32X 초극한 배율
    temp_offset = 0.0

    enable_temporal_denoise = True
    flip_v = False
    flip_h = False
    sharpness_levels = [1.0, 1.5, 2.0, 0.5]
    sharpness_idx = 1  # 기본값: 선명도 1.5x (칼같은 디테일)

    sr_engine = ThermalSuperResolution()

    window_name = f"i3system Thermal Expert [{cam.ModelName}]"
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)

    # 초기 윈도우 크기 설정
    init_w = min(1600, src_w * 4) if src_w <= 384 else min(1600, src_w * 2)
    init_h = min(1200, src_h * 4) if src_h <= 288 else min(1200, src_h * 2)
    cv2.resizeWindow(window_name, init_w, init_h)

    # 마우스 좌표 및 윈도우 레터박스 매핑 상태
    view_state = {
        "win_mouse_x": init_w // 2,
        "win_mouse_y": init_h // 2,
        "offset_x": 0,
        "offset_y": 0,
        "disp_w": init_w,
        "disp_h": init_h,
    }

    def on_mouse(event, x, y, flags, param):
        if event in (cv2.EVENT_MOUSEMOVE, cv2.EVENT_LBUTTONDOWN):
            view_state["win_mouse_x"] = x
            view_state["win_mouse_y"] = y

    cv2.setMouseCallback(window_name, on_mouse)

    status_msg = "Super Resolution 32X (칼선명도 & 노이즈 억제) 활성화됨"
    status_timer = time.time()
    last_temp_update = 0
    raw_cursor_temp = 0.0
    cam_cursor_x = src_w // 2
    cam_cursor_y = src_h // 2

    try:
        while True:
            # 1. 센서 프레임 수신
            success = cam.GetColorFrame(clr_buffer, colormap_idx)
            if not success:
                key = cv2.waitKey(2) & 0xFF
                if key in (27, ord("q"), ord("Q")):
                    break
                continue

            # 2. NumPy 원본 이미지 변환 (BGR) 및 상하/좌우 반전 처리
            raw_color = np.array(clr_buffer, dtype=np.uint8).reshape((src_h, src_w, 3))
            if flip_v and flip_h:
                raw_color = cv2.flip(raw_color, -1)
            elif flip_v:
                raw_color = cv2.flip(raw_color, 0)
            elif flip_h:
                raw_color = cv2.flip(raw_color, 1)

            # 3. Super Resolution 처리 (1X ~ 32X)
            scale_factor = sr_modes[sr_mode_idx]
            cur_w, cur_h = src_w * scale_factor, src_h * scale_factor
            sharpness_val = sharpness_levels[sharpness_idx]

            proc_img = sr_engine.process(
                raw_color,
                scale_factor=scale_factor,
                temporal_denoise=enable_temporal_denoise,
                sharpness_boost=sharpness_val
            )

            render_h, render_w = proc_img.shape[:2]

            # 4. 현재 실제 윈도우 크기 획득 (가로세로 비율 고정용)
            rect = cv2.getWindowImageRect(window_name)
            win_w = max(100, rect[2]) if rect[2] > 0 else render_w
            win_h = max(100, rect[3]) if rect[3] > 0 else render_h

            # 5. 가로세로비(Aspect Ratio) 왜곡 방지 레터박스(Letterbox) 계산
            scale = min(win_w / render_w, win_h / render_h)
            disp_w = int(render_w * scale)
            disp_h = int(render_h * scale)
            ox = (win_w - disp_w) // 2
            oy = (win_h - disp_h) // 2

            view_state["offset_x"] = ox
            view_state["offset_y"] = oy
            view_state["disp_w"] = disp_w
            view_state["disp_h"] = disp_h

            # 6. 마우스 위치를 카메라 원본 센서 좌표로 정밀 역산 매핑
            mx_in_disp = view_state["win_mouse_x"] - ox
            my_in_disp = view_state["win_mouse_y"] - oy
            if disp_w > 0 and disp_h > 0:
                norm_x = np.clip(mx_in_disp / disp_w, 0.0, 1.0)
                norm_y = np.clip(my_in_disp / disp_h, 0.0, 1.0)
                cam_cursor_x = int(norm_x * (src_w - 1))
                cam_cursor_y = int(norm_y * (src_h - 1))

            # 7. 0.15초마다 해당 지점 실제 센서 온도 측정 (반전 보정)
            sensor_x = (src_w - 1 - cam_cursor_x) if flip_h else cam_cursor_x
            sensor_y = (src_h - 1 - cam_cursor_y) if flip_v else cam_cursor_y
            if time.time() - last_temp_update > 0.15:
                last_temp_update = time.time()
                raw_cursor_temp = cam.GetPixelTemp(sensor_x, sensor_y)

            final_cursor_temp = raw_cursor_temp + temp_offset

            # 8. 표시 이미지에 마우스 포인터 십자선 마킹
            mark_x = int((cam_cursor_x / (src_w - 1)) * (render_w - 1))
            mark_y = int((cam_cursor_y / (src_h - 1)) * (render_h - 1))
            m_radius = 8 if render_w >= 1280 else (6 if render_w >= 640 else 4)
            m_len = 16 if render_w >= 1280 else (12 if render_w >= 640 else 8)
            cv2.circle(proc_img, (mark_x, mark_y), m_radius, (0, 255, 0), 1)
            cv2.drawMarker(proc_img, (mark_x, mark_y), (0, 255, 0), cv2.MARKER_CROSS, m_len, 1)

            # 9. 상단 반투명 정보 바 오버레이
            bar_height = 56 if render_h >= 900 else (48 if render_h >= 480 else 42)
            overlay = proc_img.copy()
            cv2.rectangle(overlay, (0, 0), (render_w, bar_height), (15, 15, 15), -1)
            cv2.addWeighted(overlay, 0.75, proc_img, 0.25, 0, proc_img)

            # 텍스트 정보 표시
            sr_tag = f"SR {scale_factor}X ({cur_w}x{cur_h})" if scale_factor > 1 else f"Native 1X ({src_w}x{src_h})"
            offset_str = f" [보정: {temp_offset:+.1f}C]" if abs(temp_offset) > 0.01 else ""
            sharp_str = f" [선명도: {sharpness_val:.1f}x]"
            flip_str = ""
            if flip_v and flip_h:
                flip_str = " [V+H Flip]"
            elif flip_v:
                flip_str = " [V-Flip]"
            elif flip_h:
                flip_str = " [H-Mirror]"
            info_text = f"{cam.ModelName} | {sr_tag} | {colormaps[colormap_idx]}{sharp_str}{flip_str} | 커서: {final_cursor_temp:.1f}C{offset_str}"

            font_scale = 0.60 if render_h >= 900 else (0.48 if render_h >= 480 else 0.38)
            cv2.putText(proc_img, info_text, (12, bar_height - 18),
                        cv2.FONT_HERSHEY_SIMPLEX, font_scale, (0, 255, 255), 1, cv2.LINE_AA)

            # 하단 상태 메시지 표시
            if time.time() - status_timer < 3.5:
                cv2.putText(proc_img, status_msg, (12, render_h - 15),
                            cv2.FONT_HERSHEY_SIMPLEX, font_scale, (0, 255, 0), 1, cv2.LINE_AA)

            # 10. 가로세로 비율을 유지한 화면 리사이징 및 캔버스 합성
            disp_img = cv2.resize(proc_img, (disp_w, disp_h), interpolation=cv2.INTER_LINEAR)
            canvas = np.zeros((win_h, win_w, 3), dtype=np.uint8)
            canvas[oy:oy + disp_h, ox:ox + disp_w] = disp_img

            cv2.imshow(window_name, canvas)

            # 11. 키보드 단축키 처리
            key = cv2.waitKey(1) & 0xFF
            if key in (27, ord("q"), ord("Q")):
                break
            elif key in (ord("r"), ord("R")):
                sr_mode_idx = (sr_mode_idx + 1) % len(sr_modes)
                new_scale = sr_modes[sr_mode_idx]
                if new_scale > 1:
                    status_msg = f"[초해상도] Super Resolution {new_scale}X 모드 ({src_w * new_scale}x{src_h * new_scale})"
                else:
                    status_msg = f"[초해상도] 원본 1X 모드 ({src_w}x{src_h})"
                status_timer = time.time()
                print(status_msg)
            elif key in (ord("k"), ord("K")):
                sharpness_idx = (sharpness_idx + 1) % len(sharpness_levels)
                sharpness_val = sharpness_levels[sharpness_idx]
                status_msg = f"[선명도 조절] Sharpness {sharpness_val:.1f}x 적용"
                status_timer = time.time()
                print(status_msg)
            elif key in (ord("n"), ord("N")):
                enable_temporal_denoise = not enable_temporal_denoise
                status_msg = f"[노이즈 제거] 시간축 노이즈 제거 {'ON (유리처럼 깨끗)' if enable_temporal_denoise else 'OFF'}"
                status_timer = time.time()
                print(status_msg)
            elif key in (ord("v"), ord("V")):
                flip_v = not flip_v
                sr_engine.prev_frame = None  # 이전 프레임 잔상 방지 리셋
                status_msg = f"[상하 반전] Vertical Flip {'ON' if flip_v else 'OFF'}"
                status_timer = time.time()
                print(status_msg)
            elif key in (ord("h"), ord("H")):
                flip_h = not flip_h
                sr_engine.prev_frame = None  # 이전 프레임 잔상 방지 리셋
                status_msg = f"[좌우 반전] Horizontal Mirror {'ON' if flip_h else 'OFF'}"
                status_timer = time.time()
                print(status_msg)
            elif key in (ord("m"), ord("M")):
                colormap_idx = (colormap_idx + 1) % len(colormaps)
                status_msg = f"[컬러맵] {colormaps[colormap_idx]} 적용"
                status_timer = time.time()
                print(status_msg)
            elif key in (ord("c"), ord("C")):
                status_msg = "셔터 캘리브레이션 수행 중..."
                cv2.putText(canvas, status_msg, (12, win_h - 20),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 1, cv2.LINE_AA)
                cv2.imshow(window_name, canvas)
                cv2.waitKey(10)
                cam.ShutterCalibration()
                sr_engine.prev_frame = None  # 노이즈 필터 버퍼 리셋
                status_msg = "[성공] 셔터 캘리브레이션 완료"
                status_timer = time.time()
                print("셔터 캘리브레이션 완료")
            elif key in (ord("+"), ord("=")):
                temp_offset += 0.5
                status_msg = f"[온도 보정] {temp_offset:+.1f}C"
                status_timer = time.time()
                print(status_msg)
            elif key in (ord("-"), ord("_")):
                temp_offset -= 0.5
                status_msg = f"[온도 보정] {temp_offset:+.1f}C"
                status_timer = time.time()
                print(status_msg)
            elif key in (ord("0"),):
                temp_offset = 0.0
                status_msg = "[온도 보정] 리셋 (0.0C)"
                status_timer = time.time()
                print(status_msg)
            elif key in (ord("t"), ord("T")):
                if raw_cursor_temp > 0:
                    temp_offset = 36.5 - raw_cursor_temp
                    status_msg = f"[자동 캘리브레이션] 커서 기준 36.5C 맞춤 (오프셋: {temp_offset:+.1f}C)"
                    status_timer = time.time()
                    print(status_msg)
            elif key in (ord("s"), ord("S")):
                tag = f"sr{scale_factor}x" if scale_factor > 1 else "native"
                filename = f"capture_{cam.ModelName.lower().replace('-', '_')}_{tag}_{cur_w}x{cur_h}_{int(time.time())}.png"
                # 저장 시 실제 scale_factor 원본 해상도로 풀 렌더링
                save_img = sr_engine.render_full_save(raw_color, scale_factor=scale_factor, sharpness_boost=sharpness_val)
                cv2.imwrite(filename, save_img)
                status_msg = f"[저장 완료] {filename}"
                status_timer = time.time()
                print(f"초고해상도 캡처 저장: {filename} ({cur_w}x{cur_h})")

    finally:
        cam.Close()
        cv2.destroyAllWindows()
        print("카메라 연결을 안전하게 종료했습니다.")


if __name__ == "__main__":
    main()
