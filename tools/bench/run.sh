#!/usr/bin/env bash
set -euo pipefail

bench=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(git -C "$bench" rev-parse --show-toplevel)
upstream_rev=${UPSTREAM_REV:-78dcd0d}
fork_base=$(git -C "$root" rev-parse --short "${FORK_BASE:-HEAD}")
rounds=${ROUNDS:-30}
packets=${PACKETS:-4000000}
sweep_count=${SWEEP:-4000}
sweep_packets=${SWEEP_PACKETS:-20000}
core=${CORE:-2}
read -r -a rates <<< "${RATES:-1000 8000}"
arch=$(uname -m)

case "$arch" in
    x86_64)
        cc=${CC:-clang}
        driver_flags=(-O3 -march=znver4 -std=gnu11 -funsigned-char -fno-strict-aliasing -fno-strict-overflow
            -fno-common -fno-delete-null-pointer-checks -mno-sse -mno-mmx -mno-sse2 -mno-avx -mno-80387
            -mno-fp-ret-in-387 -mno-red-zone -fno-jump-tables -falign-loops=1 -falign-functions=16
            -fcf-protection=branch -mharden-sls=all -fstack-protector-strong -ftrivial-auto-var-init=zero
            -fpatchable-function-entry=16,16 -flto=thin -mllvm -enable-pipeliner)
        link_flags=(-flto=thin -fuse-ld=lld)
        ;;
    aarch64)
        cc=${CC:-gcc}
        driver_flags=(-O2 -std=gnu11 -funsigned-char -fno-strict-aliasing -fno-strict-overflow -fno-common
            -fno-delete-null-pointer-checks -fno-omit-frame-pointer -fno-optimize-sibling-calls -fconserve-stack
            -fstack-protector-strong -ftrivial-auto-var-init=zero -mbranch-protection=pac-ret -mgeneral-regs-only
            -fpatchable-function-entry=4,2 -fmin-function-alignment=8 -fno-asynchronous-unwind-tables
            -fno-unwind-tables -fstrict-flex-arrays=3 -fzero-init-padding-bits=all)
        link_flags=()
        ;;
    *)
        echo "no kernel compiler flags are known for $arch" >&2
        exit 2
        ;;
esac

build=$(mktemp -d "$bench/build.XXXXXX")
trap 'rm -rf "$build"' EXIT
mkdir -p "$build/upstream" "$build/base" "$build/bin" "$build/sweep"
git -C "$root" archive "$upstream_rev" driver shared_definitions.h tests/config.h | tar -x -C "$build/upstream"
git -C "$root" archive "$fork_base" driver shared_definitions.h tests/config.h | tar -x -C "$build/base"
upstream_label="upstream $upstream_rev"
if [ "$arch" = aarch64 ]; then
    perl -0pi -e 's/(static inline FP_LONG FP64_DivPrecise\(FP_LONG arg_a, FP_LONG arg_b\) \{\n)#ifdef __SIZEOF_INT128__/$1#if defined(__SIZEOF_INT128__) \&\& (defined(__x86_64__) || defined(__ppc64le__))/' \
        "$build/upstream/driver/FixedMath/Fixed64.h"
    grep -q 'defined(__SIZEOF_INT128__) && (defined(__x86_64__)' "$build/upstream/driver/FixedMath/Fixed64.h"
    upstream_label="upstream $upstream_rev, its software division on ARM"
fi

driver() {
    local side=$1
    shift
    if ! "$cc" "${driver_flags[@]}" -DTEST_ENV "$@" >> "$build/compile-$side.log" 2>&1; then
        cat "$build/compile-$side.log" >&2
        exit 1
    fi
}
driver upstream -I "$bench/shim" -I "$build/upstream" -I "$build/upstream/driver" \
    -c "$build/upstream/driver/accel_modes.c" -o "$build/upstream-modes.o"
driver upstream -I "$bench/shim" -I "$bench" -I "$build/upstream" -I "$build/upstream/driver" \
    -c "$bench/upstream.c" -o "$build/upstream-entry.o"
driver fork -I "$root" -c "$root/driver/accel_modes.c" -o "$build/fork-modes.o"
driver fork -I "$root" -c "$root/driver/profile_table.c" -o "$build/fork-table.o"
driver fork -I "$root" -I "$bench" -c "$bench/fork.c" -o "$build/fork-entry.o"
driver base -I "$build/base" -c "$build/base/driver/accel_modes.c" -o "$build/base-modes.o"
driver base -I "$build/base" -c "$build/base/driver/profile_table.c" -o "$build/base-table.o"
driver base -I "$build/base" -I "$bench" -c "$bench/fork.c" -o "$build/base-entry.o"
"$cc" -O2 -I "$bench" -c "$bench/main.c" -o "$build/main.o"
"$cc" "${link_flags[@]}" "$build/main.o" "$build/upstream-modes.o" "$build/upstream-entry.o" -o "$build/bin/upstream" -lm
"$cc" "${link_flags[@]}" "$build/main.o" "$build/fork-modes.o" "$build/fork-table.o" "$build/fork-entry.o" \
    -o "$build/bin/fork" -lm
"$cc" "${link_flags[@]}" "$build/main.o" "$build/base-modes.o" "$build/base-table.o" "$build/base-entry.o" \
    -o "$build/bin/base" -lm

fork_rev=$(git -C "$root" rev-parse --short HEAD)
[ -z "$(git -C "$root" status --porcelain -- driver shared_definitions.h tools/bench)" ] || fork_rev="$fork_rev with uncommitted changes"
echo "date: $(date -u +%Y-%m-%dT%H:%MZ)"
echo "fork: $fork_rev; $upstream_label"
model=$(grep -m1 "model name" /proc/cpuinfo 2>/dev/null | cut -d: -f2 | sed "s/^ *//" || true)
[ -n "$model" ] || model=$(tr -d "\\0" < /proc/device-tree/model 2>/dev/null || echo unknown)
echo "cpu: $model ($arch), core $core, capacity $(cat "/sys/devices/system/cpu/cpu$core/cpu_capacity" 2>/dev/null || echo unknown)"
echo "compiler warnings in the driver code: upstream $(grep -c "warning:" "$build/compile-upstream.log" || true), fork $(grep -c "warning:" "$build/compile-fork.log" || true)"
echo "governor: $(cat "/sys/devices/system/cpu/cpu$core/cpufreq/scaling_governor" 2>/dev/null || echo unknown)"
echo "kernel: $(uname -r); compiler: $("$cc" --version | head -1)"
echo "driver flags: ${driver_flags[*]}"
echo "fork base for the unchanged-output check: $fork_base"
echo "packets: every 61st moves up to 2000 counts, the gaps vary by a quarter of the interval, every 997th waits 150 ms"
echo "method: $sweep_count generated settings of $sweep_packets packets compared packet by packet; $rounds rounds of $packets packets per curve timed, fork and upstream alternating which runs first"

status=0

first_difference() {
    local left=$1 right=$2 step=$3 id=$4 math=$5
    "$build/bin/$left" trace "$step" "$id" "$sweep_packets" "$math" > "$build/trace-$left"
    "$build/bin/$right" trace "$step" "$id" "$sweep_packets" "$math" > "$build/trace-$right"
    awk -v left="$left" -v right="$right" '
        $1 !~ /^([0-9]+|refused:)$/ { next }
        NR == FNR { line[++n] = $0; next }
        line[++m] != $0 {
            split(line[m], a, " ")
            if ($1 == "refused:" || a[1] == "refused:")
                printf "%s: %s; %s: %s", left, line[m], right, $0
            else
                printf "packet %d, input %d %d after %d ns: %s %d %d, %s %d %d", $1, $2, $3, $4, left, a[5], a[6], right, $5, $6
            found = 1
            exit
        }
        END { if (!found) printf "no packet differs in the trace" }' "$build/trace-$left" "$build/trace-$right"
}

first_curve_difference() {
    local left=$1 right=$2 step=$3 id=$4 math=$5
    "$build/bin/$left" curve "$step" "$id" "$math" > "$build/curve-$left"
    "$build/bin/$right" curve "$step" "$id" "$math" > "$build/curve-$right"
    awk -v left="$left" -v right="$right" '
        NR == FNR { if ($1 ~ /^[0-9]+$/) value[$1] = $2; next }
        $1 ~ /^[0-9]+$/ && value[$1] != $2 {
            if (!found) printf "the curves first differ at %.6f counts/ms: %s %.10f, %s %.10f", $1 / 4294967296, left, value[$1] / 4294967296, right, $2 / 4294967296
            found++
        }
        END { if (!found) printf "the curves are identical; the difference is outside the curve" }' "$build/curve-$left" "$build/curve-$right"
}

for rate in "${rates[@]}"; do
    step=$((1000000000 / rate))
    for side in upstream fork base; do
        "$build/bin/$side" sweep "$step" 0 "$sweep_count" "$sweep_packets" plain > "$build/sweep/$side-plain"
    done
    for side in fork base; do
        "$build/bin/$side" sweep "$step" 0 "$sweep_count" "$sweep_packets" exact > "$build/sweep/$side-exact"
    done

    echo
    echo "$rate Hz, fork against upstream, $sweep_count settings:"
    awk '
        NR == FNR { up[$1] = substr($0, length($1) + 2); next }
        {
            id = $1; f = substr($0, length($1) + 2); u = up[id]
            if (!(id in up)) class = "missing from the upstream sweep"
            else if (f ~ /^(crashed|failed)/) class = "fork " f
            else if (u ~ /^refused: upstream has none/) class = "fork-only settings, checked against the fork base only"
            else if (u ~ /^(crashed|failed)/) { class = "upstream " u; detail[class] = detail[class] " " id }
            else if (f ~ /^refused/) { class = "fork refuses, upstream runs"; reason[substr(f, 10)]++ }
            else if (f == u) class = "identical output"
            else { class = "different output"; mode[id % 10]++; if (shown[id % 10]++ < 2) print "different", id > "/dev/stderr" }
            count[class]++
        }
        END {
            for (c in count) printf "  %s: %d%s\n", c, count[c], (c in detail) ? " (ids" detail[c] ")" : ""
            for (r in reason) printf "    refused %d times: %s\n", reason[r], r
            for (m in mode) printf "    different in curve type %d: %d settings\n", m, mode[m]
        }' "$build/sweep/upstream-plain" "$build/sweep/fork-plain" 2> "$build/sweep/examples" | sort
    while read -r _ id; do
        echo "    setting $id: $(first_difference upstream fork "$step" "$id" plain)"
        echo "      $(first_curve_difference upstream fork "$step" "$id" plain)"
    done < "$build/sweep/examples"
    if grep -q -E '^[0-9]+ (crashed|failed)' "$build/sweep/fork-plain" "$build/sweep/fork-exact"; then
        echo "the fork crashed or failed on a setting" >&2
        status=1
    fi

    for math in plain exact; do
        if cmp -s "$build/sweep/base-$math" "$build/sweep/fork-$math"; then
            echo "$rate Hz, $math math: the fork's output equals $fork_base's on every setting"
            continue
        fi
        status=1
        echo "$rate Hz, $math math: the fork's output differs from $fork_base's:" >&2
        if diff "$build/sweep/base-$math" "$build/sweep/fork-$math" > "$build/sweep/diff-$math"; then
            echo "cmp and diff disagree on the $math sweep" >&2
        fi
        awk '/^>/ && n++ < 5' "$build/sweep/diff-$math" >&2
        for id in $(awk '/^>/ && n++ < 2 { print $2 }' "$build/sweep/diff-$math"); do
            echo "    setting $id: $(first_difference base fork "$step" "$id" "$math")" >&2
            echo "      $(first_curve_difference base fork "$step" "$id" "$math")" >&2
        done
    done
done

curves=(current linear power classic motivity synchronous natural jump)
for rate in "${rates[@]}"; do
    step=$((1000000000 / rate))
    : > "$build/check-upstream"
    : > "$build/check-fork"
    for curve in "${curves[@]}"; do
        "$build/bin/upstream" time "$step" 200000 plain "$curve" back-to-back >> "$build/check-upstream"
        "$build/bin/fork" time "$step" 200000 plain "$curve" back-to-back >> "$build/check-fork"
    done
    if ! diff <(cut -d' ' -f1,3 "$build/check-upstream") <(cut -d' ' -f1,3 "$build/check-fork") > "$build/check-diff"; then
        echo "at $rate Hz the timed curves' outputs differ, so no timing is reported:" >&2
        cat "$build/check-diff" >&2
        status=1
        continue
    fi
    for mode in back-to-back one-at-a-time; do
        : > "$build/times"
        for round in $(seq 1 "$rounds"); do
            if [ $((round % 2)) -eq 1 ]; then order=(fork upstream); else order=(upstream fork); fi
            for curve in "${curves[@]}"; do
                for side in "${order[@]}"; do
                    taskset -c "$core" "$build/bin/$side" time "$step" "$packets" plain "$curve" "$mode" |
                        sed "s/^/$side $round /" >> "$build/times"
                done
                taskset -c "$core" "$build/bin/fork" time "$step" "$packets" exact "$curve" "$mode" |
                    sed "s/^/exact $round /" >> "$build/times"
            done
        done
        echo
        echo "$rate Hz, $mode, ns per packet, median (interquartile range); the outputs of fork and upstream are identical"
        awk -v rounds="$rounds" '
            function sorted(list, n,    i, j, t) {
                for (i = 2; i <= n; i++) {
                    t = list[i]
                    for (j = i - 1; j >= 1 && list[j] > t; j--) list[j + 1] = list[j]
                    list[j + 1] = t
                }
            }
            function quantile(list, n, q,    position, low) {
                position = 1 + (n - 1) * q
                low = int(position)
                return list[low] + (position - low) * ((low < n ? list[low + 1] : list[low]) - list[low])
            }
            { ns[$1, $3, $2] = $4; if (!($3 in seen)) { seen[$3] = 1; order[++modes] = $3 } }
            END {
                printf "| %-11s | %-17s | %-17s | %-17s | %-24s | %-8s |\n", "Curve", "Upstream", "Fork", "Fork, exact math", "Fork - upstream, 95% CI", "Slower"
                low_rank = int(rounds / 2 - 0.98 * sqrt(rounds) + 0.5)
                high_rank = int(1 + rounds / 2 + 0.98 * sqrt(rounds) + 0.5)
                for (m = 1; m <= modes; m++) {
                    mode = order[m]
                    slower = 0
                    for (r = 1; r <= rounds; r++) {
                        u[r] = ns["upstream", mode, r]; f[r] = ns["fork", mode, r]; e[r] = ns["exact", mode, r]
                        d[r] = f[r] - u[r]
                        if (d[r] > 0) slower++
                    }
                    sorted(u, rounds); sorted(f, rounds); sorted(e, rounds); sorted(d, rounds)
                    printf "| %-11s | %6.2f (%5.2f)    | %6.2f (%5.2f)    | %6.2f (%5.2f)    | %+6.2f [%+6.2f, %+6.2f] | %2d of %2d |\n", mode,
                        quantile(u, rounds, 0.5), quantile(u, rounds, 0.75) - quantile(u, rounds, 0.25),
                        quantile(f, rounds, 0.5), quantile(f, rounds, 0.75) - quantile(f, rounds, 0.25),
                        quantile(e, rounds, 0.5), quantile(e, rounds, 0.75) - quantile(e, rounds, 0.25),
                        quantile(d, rounds, 0.5), (low_rank >= 1 ? d[low_rank] : 0), (low_rank >= 1 ? d[high_rank] : 0), slower, rounds
                }
            }' "$build/times"
    done
done
exit "$status"
