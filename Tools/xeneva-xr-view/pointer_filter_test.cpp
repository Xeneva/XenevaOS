#include "pointer_filter.h"

#include <cmath>
#include <cstdio>

static bool near(float a, float b, float tolerance) {
	return std::fabs(a - b) <= tolerance;
}

int main() {
	constexpr int64_t frame = 11111111; /* 90 Hz */
	PointerFilter filter;
	PointerFilter::Point p{0.f, 0.f};
	filter.update(0.f, 0.f, 0);
	float max_jitter = 0.f;
	for (int i = 1; i <= 180; ++i) {
		float noise = (i & 1) ? 0.001f : -0.001f;
		p = filter.update(noise, -noise, i * frame);
		if (i > 30)
			max_jitter = std::max(max_jitter, std::fabs(p.x));
	}
	if (max_jitter >= 0.00035f) {
		std::fprintf(stderr, "stationary jitter too high: %.6f\n", max_jitter);
		return 1;
	}

	/* A fast deliberate sweep must not feel stuck behind the hand. */
	filter.reset();
	filter.update(0.f, 0.f, 0);
	for (int i = 1; i <= 18; ++i)
		p = filter.update((float)i / 18.f, 0.f, i * frame);
	if (p.x < 0.90f) {
		std::fprintf(stderr, "fast sweep lagged: %.4f\n", p.x);
		return 1;
	}

	/* Reacquisition resets history instead of easing across a tracking jump. */
	filter.reset();
	p = filter.update(-0.4f, 0.2f, 100 * frame);
	if (!near(p.x, -0.4f, 1e-6f) || !near(p.y, 0.2f, 1e-6f)) {
		std::fprintf(stderr, "reset did not rebase\n");
		return 1;
	}
	return 0;
}
