# Proofs

Bounded model checking of the fixed-point code with [CBMC](https://www.cprover.org/cbmc/). Each proof
checks a property for every possible input, not for samples. Run them from the repository root:

```sh
tests/proofs/run.sh
tests/proofs/run.sh div_defined accel_lut
```

With no argument it runs every proof except div_precise.c and div_overflows.c, which are
[kept for long runs](#div_precisec-and-div_overflowsc-the-division-is-exact). Set `LINUX_HEADERS` if the
userspace kernel headers are not in `/usr/include`. On NixOS, use `nix shell nixpkgs#cbmc nixpkgs#gcc` and
point `LINUX_HEADERS` at the `include` directory of `nix build nixpkgs#linuxHeaders`. CI runs the same
script (`.github/workflows/proofs.yml`).

## Division

### div_defined.c: the division is always defined

For every pair of 64-bit operands, `FP64_DivPreciseSoft` and `FP64_DivOverflows` perform no
signed overflow, no undefined shift (including a count-leading-zeros of zero), no division by
zero and no out-of-range conversion, and every loop ends within its bound.

### div_precise.c and div_overflows.c: the division is exact

`div_precise.c` states that `FP64_DivPreciseSoft(a, b)` is the exact
quotient of a times 2^32 by b, truncated toward zero, whenever it fits in 64 bits, and saturates by
sign otherwise. `div_overflows.c` states that `FP64_DivOverflows` flags exactly the quotients
that do not fit. Both are written with multiplication only. Proving 64-bit multiply and divide
circuits equal is hard for SAT and SMT solvers, so these two may not finish. They are kept for long
runs, and the argument below does not depend on them. `run.sh` does not run them. Run each by hand
from the repository root with the flags `run.sh` gives div_defined.c:

```sh
cbmc tests/proofs/div_precise.c -I . -I "${LINUX_HEADERS:-/usr/include}" -DTEST_ENV --unwind 4 \
    --unwinding-assertions --signed-overflow-check --undefined-shift-check --div-by-zero-check \
    --conversion-check --bounds-check
```

#### Why the guard is exact

Let n = |a| * 2^32 and d = |b| > 0, and let q = floor(n / d) be the magnitude of the truncated
quotient.

- Positive quotient: q fits iff q <= 2^63 - 1 iff n / d < 2^63 iff n < 2^63 * d.
- Negative quotient: -q fits iff q <= 2^63 iff n / d < 2^63 + 1 iff n < (2^63 + 1) * d.

`FP64_DivOverflows` computes n and the bound in 128-bit unsigned arithmetic, where n < 2^96 and the
bound is below 2^127, so neither overflows (div_defined.c), and it returns n >= bound. For b = 0
the bound is 0, so the guard is true and the division saturates by the dividend's sign.

`FP64_DivPrecise` itself asks the cheaper `FP64_DivSaturates`, |a| >> 31 >= |b|, which is n >= 2^63 * d.
For a positive quotient that is the bound above. For a negative one it also takes
2^63 * d <= n < (2^63 + 1) * d, where the exact truncated quotient is -2^63, the value saturation
returns, so every result is the same. The unit test checks a = b * 2^31 and -b * 2^31, each plus
or minus 0, 1 and 2, against 128-bit division. `FP64_DivOverflows` keeps the exact bound for
the callers that refuse an out-of-range constant.

#### Why the portable division is exact

`FP64_DivPreciseSoft` is the 128-by-64 long division of Hacker's Delight (divlu), which is
Knuth's Algorithm D with base 2^32 and a two-digit quotient (The Art of Computer Programming,
volume 2, section 4.3.1). Normalising the divisor so its top bit is set bounds each estimated
quotient digit to at most two above the true digit (Knuth, Theorem B), and the two correction
loops remove that excess. Changes from the published routine:

- The magnitudes are taken as `(unsigned)(-(x + 1)) + 1`, which is the exact magnitude for every
  value including the minimum and involves no signed overflow.
- The shift by 64 - s is skipped when s = 0, where the published routine relied on a shift by 64.
- A quotient whose magnitude does not fit returns the minimum or maximum by sign instead of
  overflowing.

On x86-64 and ppc64le the driver divides with the processor's instruction after the guard, which
computes the same truncated quotient for every input the guard lets through.

#### Evidence by test

`TestFixedPointArithmetic` compares both paths against exact 128-bit division: random operands,
every divisor magnitude from 0 to 63 leading zeros, operands next to the saturation boundary with
both signs, and the edge values. `YEETMOUSE_STRESS=1` multiplies each loop's count by 10000.

## Lookup table

### accel_lut.c: the lookup table stays in its curve

Compiled with `driver/accel_modes.c`, with a loop bound that covers 257 points, the largest table the
driver accepts. For every table the driver accepts (2 to 257 points, x non-decreasing, the last two x
different) and every positive speed, `accel_lut` reads below the end of each array and inside the
curve. A table of sensitivities returns the first y for any speed at or below the first x. The harness
leaves `lut_velocity` free and checks this only when it is off. A table of velocities returns the first
y divided by the first x there. That is Raw Accel's rule, and the parity tests cover it.

CBMC sees the arrays through a pointer to the curve, so it cannot flag a read just before `lut_y`,
which lands in `lut_x`. No read goes there: past the first test the speed is above the first x, so the
search never settles on the first point and every index it reads is at least 0.

With `speed < x[0]` as the first test, the first-point property fails (checked on 128-point tables).

### lut_parse.c: the lookup table text stays in its buffers

Built with goto-cc. `FP64_DivPrecise` is replaced by a stub that returns any value. This is safe
because the division is defined for every input (see [Division](#division)), and its result cannot
move a pointer.

The check uses two 8-byte buffers, each NUL-terminated as module_param_string guarantees. The driver's
buffers are 4096 bytes, too large for a bounded check. For any bytes in the two test buffers and any
table size, `accel_lut_parse` reads and writes only inside its buffers and arrays, and returns either the
requested size or 0, never a table with missing points. Stepping over the character after a number
whether or not it is a separator makes it read past the end of a buffer whose last number has none.

## Power constants

### power_constants.c, pow_guard.c, mul_guard.c: the power constants never overflow

The constants are computed by `power_constants` in `driver/accel_modes.c`. Every step goes through
`mul_checked`, `div_checked`, `pow_checked` or the compiler's overflow-checked add and subtract, and a
step that would overflow rejects the setting. Proved whole, with the polynomial power and the portable
division inlined, the function gives no answer within four hours, so the proof is split into parts, each
for every 64-bit input:

- `mul_guard.c`: `FP64_MulOverflows` flags exactly the products whose Q32.32 value does not fit,
  and a product it lets through is exact.
- `pow_guard.c`: when `FP64_PowOverflows` lets a power through, `FP64_Pow` performs no
  signed overflow, undefined shift or out-of-range conversion inside its log and exp2 polynomials
  and returns a value that is not negative. It refuses from 2^31, where `FP64_Exp2` would shift
  a value in [1, 2) into the sign bit.
- The division: [div_defined.c](#div_definedc-the-division-is-always-defined).
- `power_constants.c`, built with goto-cc, the three helpers' bodies replaced by ones that may
  return either answer and write any value: whatever the helpers return, `power_constants` itself
  overflows nothing. Leaving one of its additions unguarded makes it fail on that addition.

The GUI and the Raw Accel converter judge the same steps in double precision with
`PowerConstantsFit`, against 2^30, one bit inside the driver's range, so a setting they pass is
one the driver accepts. The power mode tests check that over 2080 settings and compare the
driver's constants with the double-precision formulas.

## Results

One run of `run.sh` with CBMC 6.11 at 6520d08. Times depend on the machine.

| Proof             | Failing properties | Time   |
|-------------------|--------------------|--------|
| div_defined.c     | 0 of 35            | 1 s    |
| mul_guard.c       | 0 of 16            | 135 s  |
| pow_guard.c       | 0 of 75            | 75 s   |
| accel_lut.c       | 0 of 3050          | 1125 s |
| lut_parse.c       | 0 of 3049          | 405 s  |
| power_constants.c | 0 of 3399          | 1 s    |

div_precise.c gave no answer within four hours, in one run with CaDiCaL and one with Bitwuzla.
