// Release-build regression for the integer canonical-boundary predicates.
// This file includes the production header directly; it must never copy the
// predicate implementations into a test-only substitute.
#include "canonical_boundary.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {

using namespace lumina::canonical;
using Point = PointOf<int64_t>;
using Path = PathOf<int64_t>;

bool Require(bool condition, const char* label) {
  if (condition) return true;
  std::cerr << "FAIL " << label << '\n';
  return false;
}

bool RequireHalfAreaAtEveryStart(Path triangle, long double expected) {
  bool ok = true;
  for (std::size_t start = 0; start < triangle.size(); ++start) {
    ok &= Require(SignedArea<int64_t>(triangle) == expected,
                  "half-unit-signed-area");
    std::vector<SimpleRing<int64_t>> rings;
    SplitTouchingRing<int64_t>(triangle, rings);
    ok &= Require(rings.size() == 1, "half-unit-ring-retained");
    if (rings.size() == 1) {
      ok &= Require(SignedArea<int64_t>(rings[0].path) == expected,
                    "half-unit-ring-orientation");
    }
    std::rotate(triangle.begin(), triangle.begin() + 1, triangle.end());
  }
  return ok;
}

}  // namespace

int main() {
  constexpr int64_t n = 1'000'000'000'000LL;
  const Point a{n, n + 1};
  const Point b{n - 1, n};
  const Path outer{
      Point{int64_t{0}, int64_t{0}},
      Point{n, n + 1},
      Point{int64_t{0}, n + 1},
  };
  const Point inside{n - 1, n};
  const Point boundary{int64_t{0}, int64_t{0}};
  const Point outside{n + 1, n};
  const Path reversed{outer.rbegin(), outer.rend()};
  const Point same_direction_near{int64_t{1}, int64_t{0}};
  const Point same_direction_far{int64_t{2}, int64_t{0}};

  bool ok = true;
  ok &= Require(AngleBefore<int64_t>(a, b), "angle-before");
  ok &= Require(!AngleBefore<int64_t>(b, a), "angle-reverse");
  ok &= Require(
      PointLocation<int64_t>(inside, outer) == 1, "point-inside");
  ok &= Require(
      PointLocation<int64_t>(inside, reversed) == 1, "point-reversed-ring");
  ok &= Require(
      PointLocation<int64_t>(boundary, outer) == 0, "point-boundary");
  ok &= Require(
      PointLocation<int64_t>(outside, outer) == -1, "point-outside");
  ok &= Require(
      AngleBefore<int64_t>(same_direction_near, same_direction_far),
      "collinear-distance");
  ok &= Require(
      !AngleBefore<int64_t>(same_direction_far, same_direction_near),
      "collinear-distance-reverse");

  ok &= Require(
      wide::Value(wide::Product{-1, std::numeric_limits<uint64_t>::max()})
          == -1.0L,
      "negative-wide-value");
  ok &= Require(
      wide::Value(wide::Product{std::numeric_limits<int64_t>::min(), 0})
          == -std::ldexp(1.0L, 127),
      "minimum-wide-value");
  ok &= Require(wide::Value(wide::Product{1, 0}) == std::ldexp(1.0L, 64),
                "positive-wide-value");

  constexpr int64_t thin_n = 100'000'000;
  const Path thin_triangle{
      Point{int64_t{0}, int64_t{0}},
      Point{thin_n, thin_n + 1},
      Point{thin_n - 1, thin_n},
  };
  ok &= RequireHalfAreaAtEveryStart(thin_triangle, 0.5L);
  ok &= RequireHalfAreaAtEveryStart(
      Path{thin_triangle.rbegin(), thin_triangle.rend()}, -0.5L);

  const Path enclosing_square{
      Point{int64_t{-1}, int64_t{-1}},
      Point{thin_n + 2, int64_t{-1}},
      Point{thin_n + 2, thin_n + 3},
      Point{int64_t{-1}, thin_n + 3},
  };
  CanonicalNode<int64_t> thin_hole_tree;
  BuildRingsFromNoded<int64_t>(
      {enclosing_square,
       Path{thin_triangle.rbegin(), thin_triangle.rend()}}, thin_hole_tree);
  ok &= Require(thin_hole_tree.children.size() == 1,
                "half-unit-hole-shell-retained");
  if (thin_hole_tree.children.size() == 1) {
    const auto& shell = *thin_hole_tree.children[0];
    ok &= Require(!shell.is_hole && shell.children.size() == 1,
                  "half-unit-hole-attached");
    if (shell.children.size() == 1) {
      ok &= Require(shell.children[0]->is_hole
                        && SignedArea<int64_t>(shell.children[0]->path) == -0.5L,
                    "half-unit-hole-orientation");
    }
  }

  // These nested areas round to the same double on macOS arm64. Their exact
  // ordering must still attach the negative inner ring as a hole.
  constexpr int64_t large = int64_t{1} << 60;
  const Path large_shell{
      Point{int64_t{0}, int64_t{0}}, Point{large, int64_t{0}},
      Point{large, large}, Point{int64_t{0}, large},
  };
  const Path large_hole{
      Point{int64_t{1}, int64_t{1}}, Point{int64_t{1}, large - 1},
      Point{large - 1, large - 1}, Point{large - 1, int64_t{1}},
  };
  CanonicalNode<int64_t> large_tree;
  BuildRingsFromNoded<int64_t>({large_shell, large_hole}, large_tree);
  ok &= Require(large_tree.children.size() == 1,
                "exact-area-parent-ordering");
  if (large_tree.children.size() == 1) {
    const auto& shell = *large_tree.children[0];
    ok &= Require(!shell.is_hole && shell.children.size() == 1
                      && shell.children[0]->is_hole,
                  "exact-area-hole-retained");
  }

  // Fan sums can exceed signed 128 bits before cancelling. Keep the remaining
  // half-unit face in both orientations after those high limbs cancel.
  Path cancelling_fans;
  for (int repeat = 0; repeat < 128; ++repeat) {
    cancelling_fans.insert(cancelling_fans.end(), large_shell.begin(),
                           large_shell.end());
  }
  for (int repeat = 0; repeat < 128; ++repeat) {
    cancelling_fans.insert(cancelling_fans.end(), large_shell.rbegin(),
                           large_shell.rend());
  }
  cancelling_fans.insert(cancelling_fans.end(), thin_triangle.begin(),
                         thin_triangle.end());
  ok &= Require(SignedArea<int64_t>(cancelling_fans) == 0.5L,
                "area-carries-beyond-128-bits");
  std::reverse(cancelling_fans.begin(), cancelling_fans.end());
  ok &= Require(SignedArea<int64_t>(cancelling_fans) == -0.5L,
                "negative-area-carries-beyond-128-bits");

  const PointOf<long double> float_a{0.0L, 0.0L};
  const PointOf<long double> float_b{1.0L, 0.0L};
  const PointOf<long double> float_c{0.0L, 1.0L};
  const PathOf<long double> float_triangle{float_a, float_b, float_c};
  ok &= Require(
      PointLocation<long double>(PointOf<long double>{0.2L, 0.2L},
                                 float_triangle) == 1,
      "long-double-inside");
  ok &= Require(
      AngleBefore<long double>(float_b, float_c),
      "long-double-angle");

  std::cout << "canonical_predicates_regression "
            << (ok ? "PASS" : "FAIL") << '\n';
  return ok ? 0 : 1;
}
