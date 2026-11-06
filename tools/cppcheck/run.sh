#!/usr/bin/env bash
# cppcheck 정적 분석입니다. CI(.github/workflows/static-analysis.yml)가 같은 스크립트를 돌립니다.
# 사용법: bash tools/cppcheck/run.sh
# 환경 변수: CPPCHECK(실행 파일 경로), CPPCHECK_TEMPLATE(출력 형식)
set -euo pipefail
cd "$(dirname "$0")/../.."

cppcheck_bin="${CPPCHECK:-cppcheck}"
pinned="$(tr -d '[:space:]' < tools/cppcheck/version.txt)"
found="$("$cppcheck_bin" --version | awk '{print $2}')"
if [ "$found" != "$pinned" ]; then
  echo "경고: cppcheck $found 입니다. CI는 $pinned 를 쓰므로 결과가 다를 수 있습니다." >&2
fi

includes=(-I core/include -I platform/include -I app/src)
for dir in core/src/*/include; do
  includes+=(-I "$dir")
done

template='{file}:{line}:{column}: {severity}: {message} [{id}]'
template="${CPPCHECK_TEMPLATE:-$template}"

run() {
  "$cppcheck_bin" \
    --enable=warning,performance,portability \
    --std=c++20 --language=c++ \
    --check-level=exhaustive \
    --inline-suppr \
    --suppressions-list=tools/cppcheck/suppressions.txt \
    --error-exitcode=1 \
    --quiet \
    -j "$(getconf _NPROCESSORS_ONLN)" \
    --template="$template" \
    "${includes[@]}" "$@"
}

status=0

# core와 platform은 Qt를 쓰지 않습니다. qt 라이브러리 설정은 slots, signals를 매크로로 보므로 여기서는 쓰지 않습니다.
echo "core, platform"
run --library=googletest core platform || status=1

echo "app"
run --library=qt --library=googletest app/src app/test || status=1

if [ "$status" -eq 0 ]; then
  echo "cppcheck 통과"
fi
exit "$status"
