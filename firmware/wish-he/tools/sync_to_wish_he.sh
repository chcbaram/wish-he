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
# ★ 이 스크립트 자신도 뺀다. 개발 저장소 경로가 적혀 있어 배포본에 둘 것이 아니고,
#   저쪽에서 쓸 일도 없다 (복사는 언제나 이쪽에서 건다).
SELF="$(basename "$0")"

rsync -a --delete "$@" \
  --exclude 'docs/' \
  --exclude 'build/' \
  --exclude '__pycache__/' \
  --exclude '.DS_Store' \
  --exclude "$SELF" \
  "$SRC" "$DST"

# --exclude 는 대상 파일을 **보호**해서 --delete 로도 안 지워진다. 한 번이라도
# 딸려 간 적이 있으면 남으므로 여기서 직접 지운다. (-n 이면 지우지 않는다)
case " $* " in
  *" -n "*|*" --dry-run "*) ;;
  *) rm -f "$DST/tools/$SELF" ;;
esac

echo "  $SRC"
echo "    -> $DST"
