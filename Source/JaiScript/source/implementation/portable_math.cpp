// The portable math kernel (include/jaiscript/detail/portable_math.hpp): every function built from IEEE basic operations
// (+ - * / sqrt, exact on every conforming target) in a fixed order, with its data in portable_math_tables.hpp. No fused
// multiply-add, no reassociation: the pragmas keep both off under any flags a host builds with, fast-math included, save
// Clang's -ffp-contract=fast, which overrides pragmas (and which -ffast-math implies on targets without float_control,
// such as 32-bit ARM). Every special value is told apart by its bits, so an assumption of finite math changes nothing.
#include <jaiscript/detail/portable_math.hpp>
#include <jaiscript/jaiscript_fwd.hpp>
#include "portable_math_tables.hpp"

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>

#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0 && FLT_EVAL_METHOD != -1
#error "the portable math kernel needs double expressions evaluated in double (FLT_EVAL_METHOD 0)"
#endif

#if defined(__clang__)
// float_control is not available on every target (32-bit ARM ignores it); there the clang fp pragmas still hold
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-pragmas"
#pragma float_control(precise, on, push)
#pragma clang diagnostic pop
#pragma clang fp contract(off) reassociate(off)
#elif defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("no-fast-math", "fp-contract=off")
#endif

namespace jai::detail::portable_math {

namespace {

using namespace portable_math_tables;

constexpr std::uint64_t k_sign_bit = std::uint64_t{1} << 63;
constexpr std::uint64_t k_mantissa_mask = (std::uint64_t{1} << 52) - 1;
constexpr double k_nan = std::bit_cast<double>(std::uint64_t{0x7FF8000000000000});
constexpr double k_infinity = std::bit_cast<double>(std::uint64_t{0x7FF0000000000000});
constexpr std::uint64_t k_infinity_bits = 0x7FF0000000000000;
// adding and subtracting 1.5 * 2^52 rounds a double under 2^51 to the nearest integer (ties to even)
constexpr double k_round_shift = 0x1.8p52;
constexpr double k_veltkamp = 134217729.0;   // 2^27 + 1

struct dd {
	double hi;
	double lo;
};

JAI_FORCEINLINE std::uint64_t bits_of(double x) noexcept { return std::bit_cast<std::uint64_t>(x); }
JAI_FORCEINLINE double from_bits(std::uint64_t u) noexcept { return std::bit_cast<double>(u); }
JAI_FORCEINLINE double magnitude(double x) noexcept { return from_bits(bits_of(x) & ~k_sign_bit); }
JAI_FORCEINLINE double with_sign_of(double value, double sign) noexcept { return from_bits((bits_of(value) & ~k_sign_bit) | (bits_of(sign) & k_sign_bit)); }
JAI_FORCEINLINE bool sign_set(double x) noexcept { return (bits_of(x) & k_sign_bit) != 0; }
// |x|'s bits: ordered as |x| is, an infinity at k_infinity_bits and every NaN above it
JAI_FORCEINLINE std::uint64_t magnitude_bits(double x) noexcept { return bits_of(x) & ~k_sign_bit; }
constexpr std::uint64_t bits_of_constant(double x) noexcept { return std::bit_cast<std::uint64_t>(x); }
// 2^e for a normal exponent, -1022 <= e <= 1023
JAI_FORCEINLINE double power_of_two(int e) noexcept { return from_bits(static_cast<std::uint64_t>(e + 1023) << 52); }

// a + b exactly as hi + lo, given |a| >= |b| or a == 0
JAI_FORCEINLINE dd fast_two_sum(double a, double b) noexcept {
	const double s = a + b;
	return {s, b - (s - a)};
}
JAI_FORCEINLINE dd two_sum(double a, double b) noexcept {
	const double s = a + b;
	const double bb = s - a;
	return {s, (a - (s - bb)) + (b - bb)};
}
// a * b exactly as hi + lo (Dekker, Veltkamp's split): no operand above 2^995, no partial product below 2^-969
JAI_FORCEINLINE dd two_product(double a, double b) noexcept {
	const double p = a * b;
	const double ta = k_veltkamp * a, tb = k_veltkamp * b;
	const double ah = ta - (ta - a), bh = tb - (tb - b);
	const double al = a - ah, bl = b - bh;
	return {p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
}
// (n.hi + n.lo) / (d.hi + d.lo)
JAI_FORCEINLINE dd divide(dd n, dd d) noexcept {
	const double q = n.hi / d.hi;
	const dd p = two_product(q, d.hi);
	const double rest = (((n.hi - p.hi) - p.lo) + n.lo) - q * d.lo;
	return fast_two_sum(q, rest / d.hi);
}
// sqrt(d.hi + d.lo), d.hi > 0
JAI_FORCEINLINE dd square_root(dd d) noexcept {
	const double h = std::sqrt(d.hi);
	const dd hh = two_product(h, h);
	return fast_two_sum(h, (((d.hi - hh.hi) - hh.lo) + d.lo) / (h + h));
}
// c - a for a constant c = c_hi + c_lo with |c_hi| >= |a.hi|
JAI_FORCEINLINE dd subtract_from(double c_hi, double c_lo, dd a) noexcept {
	const dd s = fast_two_sum(c_hi, -a.hi);
	return fast_two_sum(s.hi, s.lo + (c_lo - a.lo));
}

// ---- sin, cos, tan ---------------------------------------------------------------------------------------------------

struct reduced {
	int quadrant;
	double hi;
	double lo;
};

// the count bits of the little-endian x ending at bit top, as an integer (a bit below 0 reads 0)
std::uint64_t bits_ending_at(const std::uint32_t* x, int top, int count) noexcept {
	std::uint64_t result = 0;
	for (int b = top; b > top - count; --b) {
		result <<= 1;
		if (b >= 0) {
			result |= (x[b >> 5] >> (b & 31)) & 1u;
		}
	}
	return result;
}

// Payne-Hanek: x * 2/pi from 192 bits of 2/pi starting where x's integer part makes the earlier bits multiples of 4, in
// integer arithmetic; the fraction leaves at least 128 significant bits for the closest double to a multiple of pi/2
JAI_NOINLINE reduced reduce_large(double x) noexcept {
	const std::uint64_t u = bits_of(x);
	const int exponent = static_cast<int>((u >> 52) & 0x7FF) - 1075;   // |x| = mantissa * 2^exponent
	const std::uint64_t mantissa = (u & k_mantissa_mask) | (std::uint64_t{1} << 52);
	// 2/pi's fraction bit i (weight 2^-i) sits at bit i + 63 of TWO_OVER_PI_BITS, counted from the first word's top (two zero
	// words lead); the window starts at fraction bit exponent - 1
	const int start = exponent + 62;
	const int word = start >> 5, shift = start & 31;
	std::uint32_t window[6];   // little-endian
	for (int k = 0; k < 6; ++k) {
		const std::uint32_t high = TWO_OVER_PI_BITS[word + 5 - k], low = TWO_OVER_PI_BITS[word + 6 - k];
		window[k] = shift == 0 ? high : (high << shift) | (low >> (32 - shift));
	}
	std::uint32_t product[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	const std::uint64_t limbs[2] = {mantissa & 0xFFFFFFFFu, mantissa >> 32};
	for (int i = 0; i < 2; ++i) {
		std::uint64_t carry = 0;
		for (int k = 0; k < 6; ++k) {
			const std::uint64_t t = limbs[i] * window[k] + product[i + k] + carry;
			product[i + k] = static_cast<std::uint32_t>(t);
			carry = t >> 32;
		}
		product[i + 6] = static_cast<std::uint32_t>(carry);
	}
	// |x| * 2/pi = product / 2^190 (mod 4)
	int quadrant = static_cast<int>(product[5] >> 30);
	product[5] &= 0x3FFFFFFFu;
	bool flip = false;
	if ((product[5] >> 29) != 0) {   // a fraction of a half or more rounds the quadrant up: 2^190 - fraction
		++quadrant;
		flip = true;
		std::uint64_t borrow = 0;
		for (int k = 0; k < 6; ++k) {
			const std::uint64_t t = std::uint64_t{0} - product[k] - borrow;
			product[k] = static_cast<std::uint32_t>(t);
			borrow = (t >> 32) != 0 ? 1 : 0;
		}
		product[5] &= 0x3FFFFFFFu;
	}
	int top = 189;
	while (top >= 0 && ((product[top >> 5] >> (top & 31)) & 1u) == 0) {
		--top;
	}
	dd r{0.0, 0.0};
	if (top >= 0) {
		const double f_hi = static_cast<double>(bits_ending_at(product, top, 53)) * power_of_two(top - 52 - 190);
		const double f_lo = static_cast<double>(bits_ending_at(product, top - 53, 53)) * power_of_two(top - 105 - 190);
		const dd p = two_product(f_hi, PIO2_HI);
		r = fast_two_sum(p.hi, p.lo + (f_hi * PIO2_LO + f_lo * PIO2_HI));
	}
	if (flip != sign_set(x)) {
		r = {-r.hi, -r.lo};
	}
	return {sign_set(x) ? -quadrant : quadrant, r.hi, r.lo};
}

// x - k pi/2 with pi/2 in three parts (k < 2^20, so k times either 33-bit part is exact); Payne-Hanek when even that cancels
JAI_NOINLINE reduced reduce_medium_close(double x, double kd) noexcept {
	const double a = x - kd * PIO2_1;
	const dd s = two_sum(a, -(kd * PIO2_2));
	const dd t = two_sum(s.hi, -(kd * PIO2_2T));
	const dd r = fast_two_sum(t.hi, t.lo + s.lo);
	if (magnitude(r.hi) < magnitude(kd) * 0x1p-59) {
		return reduce_large(x);
	}
	return {static_cast<int>(static_cast<std::int64_t>(kd)), r.hi, r.lo};
}

// x finite: x = quadrant pi/2 + (hi + lo), |hi| <= pi/4 (and a rounding). Under 2^20, pi/2 as a 33-bit part and the
// rest (k = 0 included): the error is under k 2^-86, so 23 bits of cancellation still leave 62 good bits
JAI_FORCEINLINE reduced reduce(double x) noexcept {
	if (magnitude(x) < 0x1p20) {
		const double kd = (x * TWO_OVER_PI + k_round_shift) - k_round_shift;
		const dd r = fast_two_sum(x - kd * PIO2_1, -(kd * PIO2_1T));   // exact once the test passes: then |x - k p1| > 2^9 |k p1t|
		if (magnitude(r.hi) >= magnitude(kd) * 0x1p-24) {
			return {static_cast<int>(static_cast<std::int64_t>(kd)), r.hi, r.lo};
		}
		return reduce_medium_close(x, kd);
	}
	return reduce_large(x);
}

// sin(hi + lo + quadrant pi/2) as an unrounded pair, |hi| <= pi/4 (and a rounding): an odd quadrant is a cosine. Under
// 1/16, Taylor; above, the table's nearest point a = j/128, |d| <= 1/256 and
//   sin(a + d) = sin a + (cos a sin d + sin a (cos d - 1)),  cos(a + d) = cos a + (-sin a sin d + cos a (cos d - 1))
JAI_FORCEINLINE dd quadrant_sin_pair(double hi, double lo, int quadrant) noexcept {
	const int odd = quadrant & 1;
	const double negate = from_bits(bits_of(1.0) | (static_cast<std::uint64_t>(quadrant & 2) << 62));
	const double ahi = magnitude(hi);
	const double shifted = ahi * 128.0 + k_round_shift;
	const double jd = shifted - k_round_shift;
	if (jd < 8.0) {
		const double z = hi * hi;
		if (odd != 0) {
			const double* c = COS_TAYLOR;
			return {negate, negate * (z * (-0.5 + z * (c[0] + z * (c[1] + z * (c[2] + z * c[3])))) - hi * lo)};
		}
		const double* s = SIN_TAYLOR;
		return {negate * hi, negate * (lo + hi * z * (s[0] + z * (s[1] + z * (s[2] + z * (s[3] + z * s[4])))))};
	}
	const double* t = SINCOS_128[bits_of(shifted) & 0xFF];   // the shifted sum's low mantissa bits are the integer
	const double sign = from_bits(bits_of(1.0) | (bits_of(hi) & k_sign_bit));
	const double d = ahi - jd * 0x1p-7, dlo = sign * lo;
	const double z = d * d;
	const double sin_d = d + (dlo + d * z * (SIN_TAYLOR[0] + z * SIN_TAYLOR[1]));
	const double cos_d_less_1 = z * (-0.5 + z * (COS_TAYLOR[0] + z * COS_TAYLOR[1])) - d * dlo;
	// sine row: a = sin a, b = cos a, and the sign of r (sine is odd); cosine row: a = cos a, b = -sin a
	const int o = odd * 2;
	const double a = t[o], b = t[2 - o] * static_cast<double>(1 - o);
	// the sign folds into the table terms ahead of the polynomials (exact: it is +-1)
	const double m = from_bits(bits_of(negate) ^ (bits_of(hi) & k_sign_bit & (static_cast<std::uint64_t>(odd) - 1)));
	const double ma = m * a, mb = m * b;
	return {ma, m * t[o + 1] + (mb * sin_d + ma * cos_d_less_1)};
}

// sin(x + extra pi/2) as an unrounded pair for |x| < 2^14 by one rounding to n = round(x 256/pi): n gives the quadrant,
// the table row j pi/256 (|j| <= 64) and r = x - n pi/256 (|r| <= pi/512, its error under 2^-72) together, and the
// table's identities give the value. False within 7.5 pi/256 of a multiple of pi/2, where the result is small: there the
// reduction by pi/2 and Taylor keep the relative accuracy.
JAI_FORCEINLINE bool sin_pair_by_pi_256(double x, int extra, dd& out) noexcept {
	const double shifted = x * INV_PI_256 + k_round_shift;
	const double nd = shifted - k_round_shift;
	const std::int64_t n = static_cast<std::int64_t>(bits_of(shifted) - bits_of_constant(k_round_shift));
	const std::int64_t q = (n + 64) >> 7;
	const int j = static_cast<int>(n - q * 128);
	if (j > -8 && j < 8) {
		return false;
	}
	const dd r = fast_two_sum(x - nd * PI_256_1, -(nd * PI_256_1T));
	const double z = r.hi * r.hi;
	const double sin_r_tail = r.lo + r.hi * z * (SIN_TAYLOR[0] + z * (SIN_TAYLOR[1] + z * SIN_TAYLOR[2]));
	const double cos_r_less_1 = z * (-0.5 + z * (COS_TAYLOR[0] + z * COS_TAYLOR[1])) - r.hi * r.lo;
	// sine row: a = sin(j pi/256) (odd in j), b = cos; cosine row: a = cos, b = -sin(j pi/256); the quadrant's sign on both
	const int quadrant = static_cast<int>(q) + extra;
	const std::uint64_t odd = static_cast<std::uint64_t>(quadrant & 1);
	const std::uint64_t negate = static_cast<std::uint64_t>(quadrant & 2) << 62;
	const std::uint64_t j_negative = static_cast<std::uint64_t>(j < 0) << 63;
	const double* t = SINCOS_PI_256[j < 0 ? -j : j];
	const int o = static_cast<int>(odd) * 2;
	const std::uint64_t a_sign = (j_negative & (odd - 1)) ^ negate;
	const std::uint64_t b_sign = ((j_negative ^ k_sign_bit) & (0 - odd)) ^ negate;
	const double a = from_bits(bits_of(t[o]) ^ a_sign), b = from_bits(bits_of(t[2 - o]) ^ b_sign);
	out = {a, b * r.hi + (from_bits(bits_of(t[o + 1]) ^ a_sign) + (b * sin_r_tail + a * cos_r_less_1))};
	return true;
}
// sin(x + extra pi/2) as an unrounded pair, x finite
JAI_FORCEINLINE dd quadrant_sin(double x, int extra) noexcept {
	dd out;
	if (magnitude_bits(x) < bits_of_constant(0x1p14) && sin_pair_by_pi_256(x, extra, out)) {
		return out;
	}
	const reduced r = reduce(x);
	return quadrant_sin_pair(r.hi, r.lo, r.quadrant + extra);
}

// ---- exp ---------------------------------------------------------------------------------------------------------------

// e^(x + xlo) = 2^e (hi + lo) with hi + lo = 2^(j/128) (1 + p): k = round(x 128/ln2), r = x - k ln2/128, |r| <= ln2/256
struct scaled {
	int e;
	double hi;
	double lo;
};
JAI_FORCEINLINE scaled exp_reduce(double x, double xlo) noexcept {
	const double kd = (x * INV_LN2_128 + k_round_shift) - k_round_shift;
	const double r = ((x - kd * LN2_128_HI) - kd * LN2_128_LO) + xlo;
	const double z = r * r;
	const double* c = EXP_TAYLOR;
	const double p = r + z * (c[0] + r * (c[1] + r * (c[2] + r * (c[3] + r * c[4]))));
	const std::int64_t k = static_cast<std::int64_t>(kd);
	const double* t = EXP2_128[k & 127];
	return {static_cast<int>(k >> 7), t[0], t[1] + t[0] * p};
}
// 2^e (hi + lo) rounded once, hi + lo in [1/2, 2): a subnormal result rounds on its own grid, not twice
JAI_NOINLINE double scale_subnormal(int e, double hi, double lo) noexcept {
	const double s = power_of_two(e + 1000);
	const double yh = hi * s, yl = lo * s;
	if (yh >= 0x1p-22) {
		return (yh + yl) * 0x1p-1000;
	}
	// adding 2^-22 rounds to multiples of 2^-74, the subnormal grid once scaled by 2^-1000
	const double bias = 0x1p-22;
	const double h = bias + yh;
	const double l = ((bias - h) + yh) + yl;
	return ((h + l) - bias) * 0x1p-1000;
}
JAI_FORCEINLINE double scale(int e, double hi, double lo) noexcept {
	if (e > 1023) {
		return (hi + lo) * power_of_two(e - 1) * 2.0;
	}
	if (e < -1021) {
		return scale_subnormal(e, hi, lo);
	}
	return (hi + lo) * power_of_two(e);
}
// e^a - 1 as a pair, 0 <= a <= 45: r carries its rounding too, since e^a - 1 can be as small as r
dd expm1_pair(double a) noexcept {
	const double kd = (a * INV_LN2_128 + k_round_shift) - k_round_shift;
	const dd r = two_sum(a - kd * LN2_128_HI, -(kd * LN2_128_LO));
	const double z = r.hi * r.hi;
	const double* c = EXP_TAYLOR;
	const double tail = z * (c[0] + r.hi * (c[1] + r.hi * (c[2] + r.hi * (c[3] + r.hi * c[4]))));
	if (kd == 0.0) {
		return fast_two_sum(r.hi, tail + r.lo);
	}
	const std::int64_t k = static_cast<std::int64_t>(kd);
	const double* t = EXP2_128[k & 127];
	const double s = power_of_two(static_cast<int>(k >> 7));
	const double th = t[0] * s;
	// 2^e (T_hi + T_hi r + T_hi tail + T_lo (1 + r + tail)) - 1, the first two terms exact
	const dd a_part = two_sum(th, -1.0);
	const dd b_part = two_product(th, r.hi);
	const double c_part = (t[1] + t[0] * (tail + r.lo) + t[1] * (r.hi + tail)) * s;
	const dd sum = two_sum(a_part.hi, b_part.hi);
	return fast_two_sum(sum.hi, ((sum.lo + a_part.lo) + b_part.lo) + c_part);
}

// ---- log ---------------------------------------------------------------------------------------------------------------

// x = 2^e m, m in [sqrt(1/2), sqrt(2)); c ~ 1/m from the table (16 bits), r = m c - 1 exact as hi + lo, |r| <= 2^-8;
// log x = e ln2 - log c + log1p(r)
struct log_parts {
	double e;
	dd r;
	const double* row;   // c, -log c hi, -log c lo
};
JAI_FORCEINLINE log_parts log_reduce(double x) noexcept {
	std::uint64_t u = bits_of(x);
	int e = -1023;
	if ((u >> 52) == 0) {   // subnormal
		u = bits_of(x * 0x1p52);
		e -= 52;
	}
	e += static_cast<int>(u >> 52);
	const std::uint64_t mantissa = u & k_mantissa_mask;
	const int halve = mantissa >= 0x6A09E667F3BCDu ? 1 : 0;   // m >= sqrt(2): halve it
	e += halve;
	const std::uint64_t m_bits = mantissa | (static_cast<std::uint64_t>(0x3FF - halve) << 52);
	const int index = static_cast<int>(mantissa >> 44) + 150 - 256 * halve;
	const double m = from_bits(m_bits);
	const double m_high = from_bits(m_bits & ~((std::uint64_t{1} << 27) - 1));   // 26 bits: m_high c and (m - m_high) c are exact
	const double* row = LOG_TABLE[index];
	const double c = row[0];
	return {static_cast<double>(e), two_sum(m_high * c - 1.0, (m - m_high) * c), row};
}
// log1p(r) - r for |r| <= 2^-8, to 2^-62 relative
JAI_FORCEINLINE double log1p_tail(double r) noexcept {
	const double* c = LOG1P_TAYLOR;
	return r * r * (c[0] + r * (c[1] + r * (c[2] + r * (c[3] + r * (c[4] + r * (c[5] + r * c[6]))))));
}
// log(m) = -log c + log1p(r) as a pair
JAI_FORCEINLINE dd log_mantissa(const log_parts& p) noexcept {
	const dd s = fast_two_sum(p.row[1], p.r.hi);
	return fast_two_sum(s.hi, ((s.lo + p.row[2]) + p.r.lo) + log1p_tail(p.r.hi));
}
// log x as a pair to 2^-70 relative (pow's): the square term exact, log1p to r^9
dd log_precise(double x) noexcept {
	const log_parts p = log_reduce(x);
	const double r = p.r.hi;
	const dd t1 = fast_two_sum(p.e * LN2_HI, p.row[1]);
	const dd s = fast_two_sum(t1.hi, r);
	const dd sq = two_product(r, r);
	const dd s2 = fast_two_sum(s.hi, -0.5 * sq.hi);
	const double* c = LOG1P_TAYLOR;
	const double cube = r * sq.hi * (c[1] + r * (c[2] + r * (c[3] + r * (c[4] + r * (c[5] + r * (c[6] + r * c[7]))))));
	const double lo = ((((((t1.lo + s.lo) + s2.lo) - 0.5 * sq.lo) + p.e * LN2_LO) + p.row[2]) + (p.r.lo - r * p.r.lo)) + cube;
	return fast_two_sum(s2.hi, lo);
}
// the shared special cases of log, log2 and log10; false with the result set, true for a positive finite x
JAI_FORCEINLINE bool log_argument(double x, double& result) noexcept {
	const std::uint64_t u = bits_of(x);
	if (u - 1 < k_infinity_bits - 1) {   // positive, finite and nonzero
		return true;
	}
	result = (u & ~k_sign_bit) == 0 ? -k_infinity : (u == k_infinity_bits ? k_infinity : k_nan);
	return false;
}

// ---- atan --------------------------------------------------------------------------------------------------------------

// atan(t + tlo) as a pair for 0 <= t <= 1: under 1/16 Taylor; above, the table's nearest point c = j/256 and
// atan t = atan c + atan((t - c) / (1 + t c)), |(t - c) / (1 + t c)| <= 2^-9
dd atan_reduced(double t, double tlo) noexcept {
	const double jd = (t * 256.0 + k_round_shift) - k_round_shift;
	if (jd < 16.0) {
		const double z = t * t;
		const double* a = ATAN_TAYLOR;
		const double p = t * z * (a[0] + z * (a[1] + z * (a[2] + z * (a[3] + z * (a[4] + z * (a[5] + z * a[6]))))));
		return fast_two_sum(t, tlo + p);
	}
	const double c = jd * 0x1p-8;
	const double u = (t - c) / (1.0 + t * c);
	const double uz = u * u;
	const double atan_u = u + u * uz * (ATAN_TAYLOR[0] + uz * (ATAN_TAYLOR[1] + uz * ATAN_TAYLOR[2]));
	const double* row = ATAN_256[static_cast<int>(jd)];
	return fast_two_sum(row[0], row[1] + (atan_u + tlo / (1.0 + t * t)));
}
// atan(n / d) for 0 <= n <= d as a pair (n, d pairs; the quotient's rounding carried)
JAI_FORCEINLINE dd atan_of_ratio(dd n, dd d) noexcept {
	const dd q = divide(n, d);
	return atan_reduced(q.hi, q.lo);
}
// atan2(n, d) for n, d >= 0 pairs, both nonzero: atan(n/d) or pi/2 - atan(d/n)
JAI_FORCEINLINE dd first_quadrant_angle(dd n, dd d) noexcept {
	if (n.hi <= d.hi) {
		return atan_of_ratio(n, d);
	}
	return subtract_from(PIO2_HI, PIO2_LO, atan_of_ratio(d, n));
}
// sqrt(1 - x^2) as a pair for |x| < 1, 1 - x^2 exact before the root
JAI_FORCEINLINE dd cosine_of_asin(double ax) noexcept {
	const dd square = two_product(ax, ax);
	const dd d = two_sum(1.0, -square.hi);
	return square_root(fast_two_sum(d.hi, d.lo - square.lo));
}

} // namespace

// ---- the functions -------------------------------------------------------------------------------------------------------

double sin(double x) noexcept {
	const std::uint64_t ux = magnitude_bits(x);
	if (ux < bits_of_constant(0x1p-26)) {   // sin x rounds to x (signed zeros and subnormals included)
		return x;
	}
	if (ux >= k_infinity_bits) {
		return k_nan;
	}
	const dd s = quadrant_sin(x, 0);
	return s.hi + s.lo;
}

double cos(double x) noexcept {
	const std::uint64_t ux = magnitude_bits(x);
	if (ux < bits_of_constant(0x1p-27)) {
		return 1.0;
	}
	if (ux >= k_infinity_bits) {
		return k_nan;
	}
	const dd c = quadrant_sin(x, 1);
	return c.hi + c.lo;
}

void sincos(double x, double& sin_out, double& cos_out) noexcept {
	const std::uint64_t ux = magnitude_bits(x);
	if (ux < bits_of_constant(0x1p-26)) {
		sin_out = x;
		cos_out = cos(x);
		return;
	}
	if (ux >= k_infinity_bits) {
		sin_out = cos_out = k_nan;
		return;
	}
	const dd s = quadrant_sin(x, 0), c = quadrant_sin(x, 1);
	sin_out = s.hi + s.lo;
	cos_out = c.hi + c.lo;
}

double tan(double x) noexcept {
	const std::uint64_t ux = magnitude_bits(x);
	if (ux < bits_of_constant(0x1p-27)) {   // tan x = x + x^3/3 rounds to x
		return x;
	}
	if (ux >= k_infinity_bits) {
		return k_nan;
	}
	const dd sp = quadrant_sin(x, 0), cp = quadrant_sin(x, 1);
	const dd q = divide(fast_two_sum(sp.hi, sp.lo), fast_two_sum(cp.hi, cp.lo));
	return q.hi + q.lo;
}

double asin(double x) noexcept {
	const double ax = magnitude(x);
	const std::uint64_t ux = magnitude_bits(x);
	if (ux < bits_of_constant(0x1p-26)) {   // asin x = x + x^3/6 rounds to x
		return x;
	}
	if (ux >= bits_of_constant(1.0)) {
		return ux == bits_of_constant(1.0) ? with_sign_of(PIO2_HI, x) : k_nan;
	}
	const dd c = cosine_of_asin(ax);
	const dd a = first_quadrant_angle({ax, 0.0}, c);
	return with_sign_of(a.hi + a.lo, x);
}

double acos(double x) noexcept {
	const double ax = magnitude(x);
	const std::uint64_t ux = magnitude_bits(x);
	if (ux >= bits_of_constant(1.0)) {
		if (ux != bits_of_constant(1.0)) {
			return k_nan;
		}
		return sign_set(x) ? PI_HI : 0.0;
	}
	if (ux < bits_of_constant(0x1p-55)) {   // pi/2 - x rounds to pi/2
		return PIO2_HI;
	}
	const dd a = first_quadrant_angle(cosine_of_asin(ax), {ax, 0.0});
	if (sign_set(x)) {
		const dd b = subtract_from(PI_HI, PI_LO, a);
		return b.hi + b.lo;
	}
	return a.hi + a.lo;
}

double atan(double x) noexcept {
	const double ax = magnitude(x);
	const std::uint64_t ux = magnitude_bits(x);
	if (ux < bits_of_constant(0x1p-27)) {   // atan x = x - x^3/3 rounds to x
		return x;
	}
	if (ux > bits_of_constant(0x1p66)) {   // pi/2 - 1/x rounds to pi/2
		return ux <= k_infinity_bits ? with_sign_of(PIO2_HI, x) : k_nan;
	}
	dd a;
	if (ax <= 1.0) {
		a = atan_reduced(ax, 0.0);
	} else {
		a = subtract_from(PIO2_HI, PIO2_LO, atan_of_ratio({1.0, 0.0}, {ax, 0.0}));
	}
	return with_sign_of(a.hi + a.lo, x);
}

double atan2(double y, double x) noexcept {
	const std::uint64_t ux = magnitude_bits(x), uy = magnitude_bits(y);
	if (ux > k_infinity_bits || uy > k_infinity_bits) {
		return k_nan;
	}
	double ax = magnitude(x), ay = magnitude(y);
	const bool x_negative = sign_set(x);
	if (uy == 0) {
		return x_negative ? with_sign_of(PI_HI, y) : y;
	}
	if (ux == 0) {
		return with_sign_of(PIO2_HI, y);
	}
	if (ux == k_infinity_bits) {
		if (uy == k_infinity_bits) {
			return with_sign_of(x_negative ? THREE_PIO4_HI : PIO4_HI, y);
		}
		return with_sign_of(x_negative ? PI_HI : 0.0, y);
	}
	if (uy == k_infinity_bits) {
		return with_sign_of(PIO2_HI, y);
	}
	// keep the quotient's exact remainder in range: the larger operand into [2^-900, 2^900]
	const double larger = ax > ay ? ax : ay;
	if (larger > 0x1p900) {
		ax *= 0x1p-200;
		ay *= 0x1p-200;
	} else if (larger < 0x1p-900) {
		ax *= 0x1p200;
		ay *= 0x1p200;
	}
	dd a;
	const double smaller = ax > ay ? ay : ax;
	if (smaller < (ax > ay ? ax : ay) * 0x1p-60) {   // atan q rounds to q
		a = {smaller / (ax > ay ? ax : ay), 0.0};
		if (ay > ax) {
			a = subtract_from(PIO2_HI, PIO2_LO, a);
		}
	} else {
		a = first_quadrant_angle({ay, 0.0}, {ax, 0.0});
	}
	if (x_negative) {
		a = subtract_from(PI_HI, PI_LO, a);
	}
	return with_sign_of(a.hi + a.lo, y);
}

double exp(double x) noexcept {
	const std::uint64_t ux = magnitude_bits(x);
	if (ux >= k_infinity_bits) {
		return ux > k_infinity_bits ? k_nan : (sign_set(x) ? 0.0 : k_infinity);
	}
	if (x > 709.8) {   // above ln(DBL_MAX) by more than a rounding
		return k_infinity;
	}
	if (x < -746.0) {
		return 0.0;
	}
	const scaled s = exp_reduce(x, 0.0);
	return scale(s.e, s.hi, s.lo);
}

double exp2(double x) noexcept {
	const std::uint64_t ux = magnitude_bits(x);
	if (ux >= k_infinity_bits) {
		return ux > k_infinity_bits ? k_nan : (sign_set(x) ? 0.0 : k_infinity);
	}
	if (x >= 1024.0) {
		return k_infinity;
	}
	if (x < -1076.0) {
		return 0.0;
	}
	// k = round(x 128), r = (x - k/128) ln2: the subtraction is exact
	const double kd = (x * 128.0 + k_round_shift) - k_round_shift;
	const double r = (x - kd * 0x1p-7) * LN2;
	const double z = r * r;
	const double* c = EXP_TAYLOR;
	const double p = r + z * (c[0] + r * (c[1] + r * (c[2] + r * (c[3] + r * c[4]))));
	const std::int64_t k = static_cast<std::int64_t>(kd);
	const double* t = EXP2_128[k & 127];
	return scale(static_cast<int>(k >> 7), t[0], t[1] + t[0] * p);
}

double log(double x) noexcept {
	double special = 0.0;
	if (!log_argument(x, special)) {
		return special;
	}
	const log_parts p = log_reduce(x);
	const dd t1 = fast_two_sum(p.e * LN2_HI, p.row[1]);
	const dd s = fast_two_sum(t1.hi, p.r.hi);
	return s.hi + (((((t1.lo + s.lo) + p.e * LN2_LO) + p.row[2]) + p.r.lo) + log1p_tail(p.r.hi));
}

double log2(double x) noexcept {
	double special = 0.0;
	if (!log_argument(x, special)) {
		return special;
	}
	const log_parts p = log_reduce(x);
	const dd l = log_mantissa(p);
	const dd q = two_product(l.hi, INV_LN2_HI);
	const dd s = fast_two_sum(p.e, q.hi);
	return s.hi + (s.lo + ((q.lo + l.hi * INV_LN2_LO) + l.lo * INV_LN2_HI));
}

double log10(double x) noexcept {
	double special = 0.0;
	if (!log_argument(x, special)) {
		return special;
	}
	const log_parts p = log_reduce(x);
	const dd l = log_mantissa(p);
	const dd q = two_product(l.hi, INV_LN10_HI);
	const dd s = fast_two_sum(p.e * LOG10_2_HI, q.hi);
	return s.hi + ((s.lo + p.e * LOG10_2_LO) + ((q.lo + l.hi * INV_LN10_LO) + l.lo * INV_LN10_HI));
}

double pow(double x, double y) noexcept {
	const std::uint64_t ux = magnitude_bits(x), uy_magnitude = magnitude_bits(y);
	if (uy_magnitude == 0 || bits_of(x) == bits_of_constant(1.0)) {   // pow(x, +-0) and pow(1, y) are 1, NaN or not
		return 1.0;
	}
	if (ux > k_infinity_bits || uy_magnitude > k_infinity_bits) {
		return k_nan;
	}
	// y's kind: 0 not an integer, 1 an odd integer, 2 an even integer
	const std::uint64_t uy = bits_of(y);
	const int y_exponent = static_cast<int>((uy >> 52) & 0x7FF) - 1023;
	int y_kind = 0;
	if (y_exponent >= 53) {
		y_kind = 2;
	} else if (y_exponent >= 0) {
		const std::uint64_t fraction_bits = k_mantissa_mask >> y_exponent;
		if ((uy & fraction_bits) == 0) {
			y_kind = y_exponent == 0 ? 1 : (((uy >> (52 - y_exponent)) & 1) != 0 ? 1 : 2);
		}
	}
	const double ax = magnitude(x), ay = magnitude(y);
	const bool y_negative = sign_set(y);
	if (uy_magnitude == k_infinity_bits) {
		if (ux == bits_of_constant(1.0)) {   // pow(-1, +-inf)
			return 1.0;
		}
		return (ux < bits_of_constant(1.0)) == y_negative ? k_infinity : 0.0;
	}
	if (ux == 0) {
		const double result = y_negative ? k_infinity : 0.0;
		return y_kind == 1 ? with_sign_of(result, x) : result;
	}
	if (ux == k_infinity_bits) {
		const double result = y_negative ? 0.0 : k_infinity;
		return y_kind == 1 ? with_sign_of(result, x) : result;
	}
	double sign = 1.0;
	if (sign_set(x)) {
		if (y_kind == 0) {
			return k_nan;
		}
		sign = y_kind == 1 ? -1.0 : 1.0;
	}
	if (ay > 0x1p64) {   // |log x| >= 2^-54 for x != 1, so |y log x| > 746
		return (ax > 1.0) != y_negative ? k_infinity : 0.0;
	}
	const dd l = log_precise(ax);
	const dd z = two_product(y, l.hi);
	const double z_lo = z.lo + y * l.lo;
	if (z.hi > 709.8) {
		return sign * k_infinity;
	}
	if (z.hi < -746.0) {
		return sign * 0.0;
	}
	const scaled s = exp_reduce(z.hi, z_lo);
	return sign * scale(s.e, s.hi, s.lo);
}

double sinh(double x) noexcept {
	const double ax = magnitude(x);
	const std::uint64_t ux = magnitude_bits(x);
	if (ux < bits_of_constant(0x1p-26)) {   // sinh x = x + x^3/6 rounds to x
		return x;
	}
	if (ux > bits_of_constant(710.5)) {
		return ux <= k_infinity_bits ? with_sign_of(k_infinity, x) : k_nan;
	}
	if (ax >= 22.0) {   // e^-x is below 2^-63 of e^x
		const scaled s = exp_reduce(ax, 0.0);
		return with_sign_of(scale(s.e - 1, s.hi, s.lo), x);
	}
	// (E + E / (E + 1)) / 2 with E = e^|x| - 1: no cancellation near 0
	const dd e = expm1_pair(ax);
	const dd e_plus_1 = two_sum(e.hi, 1.0);
	const dd q = divide(e, fast_two_sum(e_plus_1.hi, e_plus_1.lo + e.lo));
	const dd s = two_sum(e.hi, q.hi);
	return with_sign_of(0.5 * (s.hi + ((s.lo + e.lo) + q.lo)), x);
}

double cosh(double x) noexcept {
	const double ax = magnitude(x);
	const std::uint64_t ux = magnitude_bits(x);
	if (ux < bits_of_constant(0x1p-27)) {   // 1 + x^2/2 rounds to 1
		return 1.0;
	}
	if (ux > bits_of_constant(710.5)) {
		return ux <= k_infinity_bits ? k_infinity : k_nan;
	}
	const scaled s = exp_reduce(ax, 0.0);
	if (ax >= 22.0) {
		return scale(s.e - 1, s.hi, s.lo);
	}
	const double f = power_of_two(s.e);
	const dd e = fast_two_sum(s.hi * f, s.lo * f);
	const dd inverse = divide({1.0, 0.0}, e);
	const dd sum = two_sum(e.hi, inverse.hi);
	return 0.5 * (sum.hi + ((sum.lo + e.lo) + inverse.lo));
}

double tanh(double x) noexcept {
	const double ax = magnitude(x);
	const std::uint64_t ux = magnitude_bits(x);
	if (ux < bits_of_constant(0x1p-27)) {   // tanh x = x - x^3/3 rounds to x
		return x;
	}
	if (ux > bits_of_constant(22.0)) {   // 1 - 2e^-2x rounds to 1
		return ux <= k_infinity_bits ? with_sign_of(1.0, x) : k_nan;
	}
	// E / (E + 2) with E = e^(2|x|) - 1
	const dd e = expm1_pair(ax + ax);
	const dd e_plus_2 = two_sum(e.hi, 2.0);
	const dd q = divide(e, fast_two_sum(e_plus_2.hi, e_plus_2.lo + e.lo));
	return with_sign_of(q.hi + q.lo, x);
}

double cbrt(double x) noexcept {
	std::uint64_t u = magnitude_bits(x);
	if (u >= k_infinity_bits || u == 0) {   // +-0 and +-inf are their own roots
		return u > k_infinity_bits ? k_nan : x;
	}
	int adjust = 0;
	if (u < 0x0010000000000000u) {   // subnormal: 2^54 x, then 2^-18 the root
		u = bits_of(magnitude(x) * 0x1p54);
		adjust = -18;
	}
	const int e = static_cast<int>(u >> 52) - 1023;   // |x| = 2^(3q + i) m, m in [1, 2)
	const int q = (e + 1200) / 3 - 400;
	const int i = e - 3 * q;
	const double m = from_bits((u & k_mantissa_mask) | (std::uint64_t{0x3FF} << 52));
	const double big_m = m * (i == 0 ? 1.0 : (i == 1 ? 2.0 : 4.0));
	const double* g = CBRT_GUESS;
	const double guess = (g[0] + m * (g[1] + m * (g[2] + m * (g[3] + m * (g[4] + m * g[5]))))) * CBRT_2_I[i];
	// the guess to 17 bits, so y^3 is exact and so is M - y^3; then the root of 1 + d by its series
	const double y = from_bits((bits_of(guess) + (std::uint64_t{1} << 35)) & ~((std::uint64_t{1} << 36) - 1));
	const double y3 = y * y * y;
	const double d = (big_m - y3) / y3;
	const double* c = CBRT_CORRECTION;
	const double root = y + y * (d * (c[0] + d * (c[1] + d * (c[2] + d * c[3]))));
	return with_sign_of(root * power_of_two(q + adjust), x);
}

double hypot(double x, double y) noexcept {
	const std::uint64_t ux = magnitude_bits(x), uy = magnitude_bits(y);
	if (ux == k_infinity_bits || uy == k_infinity_bits) {
		return k_infinity;
	}
	if (ux > k_infinity_bits || uy > k_infinity_bits) {
		return k_nan;
	}
	double ax = magnitude(x), ay = magnitude(y);
	if (ax < ay) {
		const double t = ax;
		ax = ay;
		ay = t;
	}
	if (ay == 0.0 || static_cast<int>(bits_of(ax) >> 52) - static_cast<int>(bits_of(ay) >> 52) > 60) {   // ax (1 + (ay/ax)^2/2) rounds to ax
		return ax + ay;
	}
	double unscale = 1.0;
	if (ax > 0x1p500) {
		ax *= 0x1p-600;
		ay *= 0x1p-600;
		unscale = 0x1p600;
	} else if (ay < 0x1p-450) {
		ax *= 0x1p600;
		ay *= 0x1p600;
		unscale = 0x1p-600;
	}
	const dd xx = two_product(ax, ax), yy = two_product(ay, ay);
	const dd s = fast_two_sum(xx.hi, yy.hi);
	const dd root = square_root(fast_two_sum(s.hi, (s.lo + xx.lo) + yy.lo));
	return (root.hi + root.lo) * unscale;
}

} // namespace jai::detail::portable_math

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-pragmas"
#pragma float_control(pop)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma float_control(pop)
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
