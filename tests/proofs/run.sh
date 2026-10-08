#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."
headers=${LINUX_HEADERS:-/usr/include}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

includes=(-I . -I "$headers" -DTEST_ENV)
defined=(--signed-overflow-check --undefined-shift-check --div-by-zero-check --conversion-check)

prove() {
    case $1 in
        div_defined)
            cbmc tests/proofs/div_defined.c "${includes[@]}" --unwind 4 --unwinding-assertions "${defined[@]}" \
                --bounds-check
            ;;
        mul_guard)
            cbmc tests/proofs/mul_guard.c "${includes[@]}" --unwind 4 --unwinding-assertions "${defined[@]}"
            ;;
        pow_guard)
            cbmc tests/proofs/pow_guard.c "${includes[@]}" --unwind 70 --unwinding-assertions "${defined[@]}" \
                --bounds-check --pointer-check
            ;;
        accel_lut)
            cbmc tests/proofs/accel_lut.c driver/accel_modes.c "${includes[@]}" --function main --no-standard-checks \
                --bounds-check --pointer-check --unwind 260 --unwinding-assertions --slice-formula
            ;;
        lut_parse)
            goto-cc tests/proofs/lut_parse.c "${includes[@]}" --function main -o "$work/lut_parse.goto"
            goto-instrument --remove-function-body FP64_DivPrecise "$work/lut_parse.goto" "$work/lut_parse.bare.goto"
            goto-instrument --generate-function-body FP64_DivPrecise --generate-function-body-options nondet-return \
                "$work/lut_parse.bare.goto" "$work/lut_parse.ready.goto"
            cbmc "$work/lut_parse.ready.goto" --no-standard-checks --bounds-check --pointer-check --unwind 20 \
                --unwinding-assertions
            ;;
        power_constants)
            goto-cc tests/proofs/power_constants.c "${includes[@]}" --function main -o "$work/power.goto"
            goto-instrument --remove-function-body mul_checked --remove-function-body div_checked \
                --remove-function-body pow_checked "$work/power.goto" "$work/power.bare.goto"
            goto-instrument --generate-function-body 'mul_checked|div_checked|pow_checked' \
                --generate-function-body-options 'havoc,params:.*' "$work/power.bare.goto" "$work/power.ready.goto"
            cbmc "$work/power.ready.goto" --unwind 70 --unwinding-assertions "${defined[@]}" --bounds-check \
                --pointer-check
            ;;
        *)
            echo "unknown proof: $1 (div_defined, mul_guard, pow_guard, accel_lut, lut_parse, power_constants)" >&2
            return 2
            ;;
    esac
}

if [ "$#" -eq 0 ]; then
    set -- div_defined mul_guard pow_guard accel_lut lut_parse power_constants
fi
failed=0
for proof in "$@"; do
    echo "== $proof"
    start=$SECONDS
    if prove "$proof" > "$work/$proof.log" 2>&1; then
        echo "holds: $(grep -E '^\*\* [0-9]+ of [0-9]+ failed' "$work/$proof.log" | tail -1), $((SECONDS - start)) s"
    else
        failed=1
        tail -40 "$work/$proof.log"
        echo "FAILS: $proof"
    fi
done
exit "$failed"
