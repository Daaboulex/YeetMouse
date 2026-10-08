# Proofs

Bounded model checking of the fixed-point code with [CBMC](https://www.cprover.org/cbmc/). Each
harness states a property for every possible input, not for samples. Run from the repository root
with the userspace Linux headers on the include path:

```sh
cbmc tests/proofs/<harness>.c -I . -I <linux-headers>/include <flags>
```

On NixOS: `nix shell nixpkgs#cbmc nixpkgs#gcc`, headers from `nix build nixpkgs#linuxHeaders`.

## div_defined.c: the division is always defined

Flags: `--unwind 4 --unwinding-assertions --signed-overflow-check --undefined-shift-check
--div-by-zero-check --conversion-check --bounds-check`

For every pair of 64-bit operands, `FP64_DivPreciseSoft` and `FP64_DivOverflows` perform no
signed overflow, no undefined shift (including a count-leading-zeros of zero), no division by
zero and no out-of-range conversion, and every loop ends within its bound. Result: 37 of 37
properties hold.

## accel_lut.c: the lookup table stays in its curve

Compile together with `driver/accel_modes.c -DTEST_ENV`. Flags: `--function main
--no-standard-checks --bounds-check --pointer-check --unwind 130 --unwinding-assertions
--slice-formula`

For every table the driver accepts (2 to 128 points, x non-decreasing, the last two x different)
and every positive speed, `accel_lut` reads below the end of each array and inside the curve, and
returns the first y at or below the first x. The arrays are reached through a pointer to the
curve, so CBMC checks the start of an array only against the start of the curve: a read just
before `lut_y` lands in `lut_x` and is not flagged. The index arithmetic is unchanged since
82e8f8b, where the arrays were separate globals and CBMC proved both bounds of every read. Result:
holds; with the old `speed < x[0]` test the second property fails.

## lut_parse.c: the lookup table text stays in its buffers

Built with `goto-cc -DTEST_ENV --function main`, then `goto-instrument --remove-function-body
FP64_DivPrecise` and `--generate-function-body FP64_DivPrecise --generate-function-body-options
nondet-return` (div_defined.c proves the division defined; its value cannot move a pointer), then
`cbmc --no-standard-checks --bounds-check --pointer-check --unwind 20 --unwinding-assertions`.

For any bytes in both buffers (each NUL-terminated, the guarantee of module_param_string) and any
table size, `accel_lut_parse` reads and writes only inside its buffers and arrays, and returns
either the requested size or 0, never a table with missing points. Result: 0 of 2112 properties
fail, 334 s on the M1. With the old unconditional step over a separator it reads past the end of
a buffer whose last number has none.

## div_precise.c and div_overflows.c: the division is exact

Flags as for div_defined.c. `div_precise.c` states that `FP64_DivPreciseSoft(a, b)` is the exact
quotient of a times 2^32 by b, truncated toward zero, whenever it fits in 64 bits, and saturates by
sign otherwise. `div_overflows.c` states that `FP64_DivOverflows` flags exactly the quotients
that do not fit. Both are written with multiplication only. Proving two 64-bit multiplier or
divider circuits equal is hard for SAT and SMT solvers and can run for hours or days; these two
are kept for long runs. The argument below does not depend on them. On 2026-10-08 four-hour runs of
div_precise.c on the M1, one with CaDiCaL and one with Bitwuzla, ended without an answer.

### Why the guard is exact

Let n = |a| * 2^32 and d = |b| > 0, and let q = floor(n / d) be the magnitude of the truncated
quotient.

- Positive quotient: q fits iff q <= 2^63 - 1 iff n / d < 2^63 iff n < 2^63 * d.
- Negative quotient: -q fits iff q <= 2^63 iff n / d < 2^63 + 1 iff n < (2^63 + 1) * d.

`FP64_DivOverflows` computes n and the bound in 128-bit unsigned arithmetic, where n < 2^96 and the
bound is below 2^127, so neither overflows (div_defined.c), and it returns n >= bound. For b = 0
the bound is 0, so the guard is true and the division saturates by the dividend's sign.

### Why the portable division is exact

`FP64_DivPreciseSoft` is the 128-by-64 long division of Hacker's Delight (divlu), which is
Knuth's Algorithm D with base 2^32 and a two-digit quotient (The Art of Computer Programming,
volume 2, section 4.3.1). Normalising the divisor so its top bit is set bounds each estimated
quotient digit to at most two above the true digit (Knuth, Theorem B), and the two correction
loops remove that excess. The changes against the published routine:

- The magnitudes are taken as `(unsigned)(-(x + 1)) + 1`, which is the exact magnitude for every
  value including the minimum and involves no signed overflow.
- The shift by 64 - s is skipped when s = 0, where the published routine relied on a shift by 64.
- A quotient whose magnitude does not fit returns the minimum or maximum by sign instead of
  overflowing.

On x86-64 and ppc64le the driver divides with the processor's instruction after the guard, which
computes the same truncated quotient for every input the guard lets through.

### Evidence by test

`TestFixedPointArithmetic` compares both paths against exact 128-bit division: random operands,
every divisor magnitude from 0 to 63 leading zeros, operands next to the saturation boundary with
both signs, and the edge values. `YEETMOUSE_STRESS=1` scales it to about two billion cases.

## power_constants.c, pow_guard.c, mul_guard.c: the power constants never overflow

The constants are computed by `power_constants` in `driver/accel_modes.c`, every step through
`mul_checked`, `div_checked`, `pow_checked` or the compiler's add and subtract overflow
builtins; a step that would not fit refuses the setting. Proving the whole function at once with
the polynomial power and the portable division inlined did not finish in four hours, so the proof
is split, each part for every 64-bit input:

- `mul_guard.c` (flags as for div_defined.c without the bounds options): `FP64_MulOverflows`
  flags exactly the products whose Q32.32 value does not fit, and a product it lets through is
  exact. 0 of 16 properties fail, 143 s on the M1.
- `pow_guard.c` (`--unwind 70 --unwinding-assertions` plus every check of div_defined.c and
  `--pointer-check`): when `FP64_PowOverflows` lets a power through, `FP64_Pow` performs no
  signed overflow, undefined shift or out-of-range conversion inside its log and exp2 polynomials
  and returns a value that is not negative. It refuses from 2^31, where `FP64_Exp2` would shift
  a value in [1, 2) into the sign bit. 0 of 75 fail, 78 s.
- The division: div_defined.c above.
- `power_constants.c`, built with `goto-cc`, the three helpers' bodies removed with
  `goto-instrument --remove-function-body` and replaced by `--generate-function-body
  'mul_checked|div_checked|pow_checked' --generate-function-body-options 'havoc,params:.*'`,
  so each may return either answer and write any value, then `cbmc` with the pow_guard.c flags:
  whatever the helpers return, `power_constants` itself overflows nothing. 0 of 2404 fail; with
  one of its additions unguarded it fails on that addition.

The GUI and the Raw Accel converter judge the same steps in double precision with
`PowerConstantsFit`, against 2^30, one bit inside the driver's range, so a setting they pass is
one the driver accepts. The power mode tests check that over 2080 settings and compare the
driver's constants with the double-precision formulas.
