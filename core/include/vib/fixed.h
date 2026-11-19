#pragma once
// 16.16 fixed-point arithmetic with the same rounding as the original game,
// so positions, rates and speeds come out identical on every platform.

#include <cstdint>

namespace vib {

class Fixed {
 public:
  constexpr Fixed() = default;
  static constexpr Fixed raw(std::int32_t r) {
    Fixed f;
    f.v_ = r;
    return f;
  }
  static constexpr Fixed whole(int n) { return raw(static_cast<std::int32_t>(std::uint32_t(n) << 16)); }
  static constexpr Fixed from(double x) { return raw(static_cast<std::int32_t>(x * 65536.0)); }
  static constexpr Fixed from(float x) { return raw(static_cast<std::int32_t>(x * 65536.0f)); }

  constexpr std::int32_t raw() const { return v_; }
  constexpr int integer() const { return v_ >> 16; }
  constexpr double to_double() const { return v_ / 65536.0; }

  constexpr Fixed operator+(Fixed o) const { return wrap(std::int64_t(v_) + o.v_); }
  constexpr Fixed operator-(Fixed o) const { return wrap(std::int64_t(v_) - o.v_); }
  constexpr Fixed operator-() const { return wrap(-std::int64_t(v_)); }
  constexpr Fixed operator*(Fixed o) const { return wrap((std::int64_t(v_) * o.v_) >> 16); }
  constexpr Fixed times(std::int32_t n) const { return wrap(std::int64_t(v_) * n); }  // by a whole number
  constexpr Fixed over(std::int32_t n) const { return raw(v_ / n); }                 // by a whole number
  // Fixed by fixed: the original keeps only 8 fraction bits of each side.
  constexpr Fixed operator/(Fixed o) const {
    const auto divisor = static_cast<std::int16_t>(o.v_ >> 8);
    return wrap((std::int64_t(v_) * 256) / divisor);
  }
  constexpr auto operator<=>(const Fixed&) const = default;

 private:
  static constexpr Fixed wrap(std::int64_t x) { return raw(static_cast<std::int32_t>(static_cast<std::uint32_t>(x))); }
  std::int32_t v_ = 0;
};

}  // namespace vib
