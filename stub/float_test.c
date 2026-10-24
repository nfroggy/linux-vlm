/* Check signed Alpha floating-point conversions, including the startup case. */
#include "std.h"
#include <fenv.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;

#include "float2"

/* Keep conversion at runtime so native instructions and FP flags are tested. */
static __attribute__((noinline)) u64 convert(double value)
{
  fregstate[1] = FPS_T;
  return _CVTTQ(2, 1, U64FLTT(value));
}

int main(void)
{
  static const struct {
    double input;
    s64 expected;
  } cases[] = {
    {-2.0, -2},
    {-2.75, -2},
    {2.75, 2},
    {-0.5, 0},
    {0.0, 0},
    {-0.0, 0},
    {-2147483648.0, INT64_C(-2147483648)},
    {2147483648.0, INT64_C(2147483648)},
    {-0x1p63, INT64_MIN},
    {0x1.fffffffffffffp62, INT64_C(9223372036854774784)}
  };
  fenv_t saved;
  int failures = 0;
  size_t i;

  /* Record invalid exceptions without terminating the test process. */
  if (feholdexcept(&saved) != 0)
    return EXIT_FAILURE;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    volatile double input = cases[i].input;
    u64 result;
    int invalid;

    feclearexcept(FE_ALL_EXCEPT);
    result = convert(input);
    invalid = fetestexcept(FE_INVALID);
    if (result != (u64)cases[i].expected || invalid || fregstate[2] != FPS_Q) {
      fprintf(stderr, "CVTTQ(%g): got %lld, expected %lld, invalid=%d\n",
              input, (long long)(s64)result,
              (long long)cases[i].expected, invalid);
      failures++;
    }
  }

  fesetenv(&saved);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
