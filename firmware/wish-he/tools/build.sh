#!/bin/sh
#
# 보드 하나를 빌드한다.
#
#     tools/build.sh                 기본 (wish60-he-7u)
#     tools/build.sh wish61-he       그 키보드
#     tools/build.sh wish61-he -c    처음부터 다시
#
# ★ 빌드 폴더와 출력 이름에 **키보드 이름을 박는다.**
#
#   두 보드가 같은 이름을 내면 엉뚱한 것을 굽기 쉽다. 부트로더가 형식을 검사해
#   벽돌이 되진 않지만 "왜 안 뜨지" 로 한참 헤맬 자리다.
#
#     build-wish60-he-7u/wish60-he-7u-tag.bin
#     build-wish61-he/wish61-he-image.bin
#
set -e

KB=${1:-wish60-he-7u}

# 키보드 -> 보드(부트로더 계약). 1:1 이지만 축이 다르므로 표로 둔다.
case "$KB" in
  wish60-he-7u) BOARD=wish60-he ;;
  wish61-he)    BOARD=wish61-he ;;
  *) echo "모르는 키보드: $KB  (keyboards/ 를 볼 것)" >&2; exit 1 ;;
esac

DIR="build-$KB"
[ "$2" = "-c" ] && rm -rf "$DIR"

cmake -DHW_KEYBOARD="$KB" -DHW_BOARD="$BOARD" -S . -B "$DIR" >/dev/null
cmake --build "$DIR" -j8

echo
echo "  $DIR/"
ls -1 "$DIR"/*.bin 2>/dev/null | sed 's|^|    |'
