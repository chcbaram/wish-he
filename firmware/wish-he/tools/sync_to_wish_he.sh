#!/bin/sh
#
# 이 펌웨어를 배포용 저장소(wish-he)로 복사한다.
#
#     ./tools/sync_to_wish_he.sh              복사
#     ./tools/sync_to_wish_he.sh -n           무엇이 바뀌는지만 본다 (dry-run)
#
# ★ 코드는 **여기서만** 고친다. 저쪽은 배포용 사본이라, 저쪽에서 고치면 다음
#   복사 때 --delete 로 지워진다.
#
# ★ docs/ 는 뺀다. 개발 기록이라 배포본에 넣지 않는다.
#   (그래서 소스 주석의 `docs/board-iap.md` 같은 링크는 저쪽에서 뜬다 — 의도한 것이다.)
#
# ★ build/ 도 뺀다. .elf / .asm / .bin 이 들어 있고 150MB 쯤 된다.
#   어셈코드와 바이너리는 깃에 올리지 않는다. 저쪽 .gitignore 에도 넣어 뒀지만
#   애초에 복사하지 않는 편이 낫다.
#
set -e

SRC="$(cd "$(dirname "$0")/.." && pwd)/"
DST="/Users/hancheol/hdd/git/wish-he/firmware/wish-he/"

if [ ! -d "$(dirname "$DST")" ]; then
  echo "[E_] 대상이 없다: $DST"
  exit 1
fi

mkdir -p "$DST"
rsync -a --delete "$@" \
  --exclude 'docs/' \
  --exclude 'build/' \
  --exclude '__pycache__/' \
  --exclude '.DS_Store' \
  "$SRC" "$DST"

echo "  $SRC"
echo "    -> $DST"
