#include <cstddef>

/* The freestanding math header supplies unqualified mixed-type min/max. */
template <typename T, typename U>
constexpr auto min(T a, U b) -> decltype(a < b ? a : b) {
	return a < b ? a : b;
}

template <typename T, typename U>
constexpr auto max(T a, U b) -> decltype(a > b ? a : b) {
	return a > b ? a : b;
}
