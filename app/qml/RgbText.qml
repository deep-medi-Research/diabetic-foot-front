pragma Singleton
import QtQuick

// RGB 패널의 숫자 문장입니다. 패널 안 여러 컴포넌트가 같은 형식으로 쓰도록 한곳에 둡니다.
QtObject {
    // RgbTrace::latest 결과의 채널 평균입니다. 표본이 없으면 "-"입니다. 250 이상은 소수 한 자리까지 써서, 반올림한 255(254.5 이상)와
    // 모든 화소가 잘린 255를 구분합니다. 소수 자리는 버려서 255.0은 실제로 255일 때만 나옵니다.
    function value(latest, key) {
        if (!latest || !latest.valid) return "-"
        const v = latest[key]
        return v >= 250 ? (Math.floor(v * 10) / 10).toFixed(1) : String(Math.round(v))
    }
    // 포화 비율(%)입니다. 0보다 크면 0.1% 아래여도 0으로 보이지 않게 씁니다.
    function saturation(v) { return v <= 0 ? "0%" : v < 0.1 ? "0.1% 미만" : v.toFixed(1) + "%" }
}
