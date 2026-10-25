#!/usr/bin/env bash
set -euo pipefail
REF=${REF:-HEAD}
DONOR=${DONOR:-clover/lineage-20.0}
OUT=${OUT:-/tmp/clover}
mkdir -p "$OUT"

ALLOW='^(drivers/soc/qcom/|drivers/power/supply/qcom/|drivers/power/reset/|drivers/thermal/qcom/|drivers/gpu/msm/|drivers/staging/android/|drivers/mmc/host/sdhci-msm|drivers/cpuidle/|drivers/input/|arch/arm64/kernel/)'
DENY='^(net/|drivers/net/|drivers/usb/host/|fs/fuse/|fs/overlayfs/|fs/pstore/|drivers/clk/qcom/gdsc\.c|drivers/cpufreq/|drivers/soc/qcom/(icnss2|rpmh|qmi_rmnet|dfc|wda)|drivers/soc/qcom/qcom_ipcc)'

git cherry -v "$REF" "$DONOR" | grep '^+' > "$OUT/cherry.txt" || true
echo "cherry +: $(wc -l < "$OUT/cherry.txt")"

git log --format=%s "$REF" | sort -u > "$OUT/local-subj.txt"
awk 'NR==FNR{seen[$0]=1; next}
     { sha=$2; $1=$2=""; sub(/^ +/,""); if (!($0 in seen)) print sha"\t"$0 }' \
    "$OUT/local-subj.txt" "$OUT/cherry.txt" > "$OUT/new.tsv"
echo "after subject filter: $(wc -l < "$OUT/new.tsv")"

: > "$OUT/filtered.tsv"
while IFS=$'\t' read -r sha subj; do
  files=$(git show --format= --name-only "$sha")
  [ -z "$files" ] && continue
  [ "$(echo "$files" | wc -l)" -gt 20 ] && continue
  echo "$files" | grep -Eq "$DENY" && continue
  echo "$files" | grep -Evq "$ALLOW" && continue
  printf '%s\t%s\n' "$sha" "$subj" >> "$OUT/filtered.tsv"
done < "$OUT/new.tsv"
echo "after path filter: $(wc -l < "$OUT/filtered.tsv")"

cut -f1 "$OUT/filtered.tsv" | HEAD_REF="$REF" ./api-check.sh > "$OUT/api.txt" 2>&1
: > "$OUT/apply.txt"
for sha in $(awk '$2=="OK"{print $1}' "$OUT/api.txt"); do
  if git show --binary "$sha" | git apply --check - 2>/dev/null; then st=CLEAN; else st=CONFLICT; fi
  printf '%s\t%s\t%s\n' "$st" "$sha" "$(git show -s --format=%s "$sha")" >> "$OUT/apply.txt"
done
echo "CLEAN: $(grep -c '^CLEAN' "$OUT/apply.txt")  CONFLICT: $(grep -c '^CONFLICT' "$OUT/apply.txt")"
echo "MISSING (api): $(grep -c MISSING "$OUT/api.txt")"
