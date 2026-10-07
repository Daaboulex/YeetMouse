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

## div_precise.c and div_overflows.c: the division is exact

Flags as for div_defined.c. `div_precise.c` states that `FP64_DivPreciseSoft(a, b)` is the exact
quotient of a times 2^32 by b, truncated toward zero, whenever it fits in 64 bits, and saturates by
sign otherwise. `div_overflows.c` states that `FP64_DivOverflows` flags exactly the quotients
that do not fit. Both are written with multiplication only. Proving two 64-bit multiplier or
divider circuits equal is hard for SAT and SMT solvers and can run for hours or days; these two
are kept for long runs. The argument below does not depend on them.

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
