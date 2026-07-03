#pragma once

/* M_PI is a _GNU_SOURCE extension, and the engine gates _GNU_SOURCE on
 * !WIN32, so MinGW (Windows cross-compile) does not see M_PI from
 * <math.h>. Top-level engine .c files should use SLEIPNER_PI instead of
 * M_PI to stay portable across all build targets. */
#define SLEIPNER_PI 3.14159265358979323846F
