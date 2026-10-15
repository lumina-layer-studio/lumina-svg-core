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

uint64_t L1Perimeter(const Path& path) {
  uint64_t total = 0;
  for (std::size_t index = 0; index < path.size(); ++index) {
    const auto direction = path[(index + 1) % path.size()] - path[index];
    total += static_cast<uint64_t>(std::abs(direction.x))
        + static_cast<uint64_t>(std::abs(direction.y));
  }
  return total;
}

bool RequireConsistentRoundedCrossings() {
  const Path first{{100000000, -100000000}, {-599999997, 600000004},
                   {-399999998, 400000003}};
  const Path second{{0, 0}, {1, 1}, {0, 1}};
  bool ok = true;
  for (int reverse_first = 0; reverse_first < 2; ++reverse_first) {
    for (int reverse_second = 0; reverse_second < 2; ++reverse_second) {
      for (std::size_t start_first = 0; start_first < first.size(); ++start_first) {
        for (std::size_t start_second = 0; start_second < second.size();
             ++start_second) {
          Path a = first;
          Path b = second;
          if (reverse_first) std::reverse(a.begin(), a.end());
          if (reverse_second) std::reverse(b.begin(), b.end());
          std::rotate(a.begin(), a.begin() + start_first, a.end());
          std::rotate(b.begin(), b.begin() + start_second, b.end());
          for (int swap = 0; swap < 2; ++swap) {
            const std::vector<Path> paths = swap ? std::vector<Path>{b, a}
                                                : std::vector<Path>{a, b};
            BoundaryNoder<int64_t> noder(paths);
            const auto noded = noder.Run();
            ok &= Require(noded.size() == paths.size(), "noding-ring-ownership");
            for (std::size_t ring = 0; ring < paths.size(); ++ring) {
              ok &= Require(L1Perimeter(noded[ring]) == L1Perimeter(paths[ring]),
                            "rounded-crossing-preserves-ring-l1");
              for (const auto& point : noded[ring]) {
                if (std::find(paths[ring].begin(), paths[ring].end(), point)
                    == paths[ring].end()) {
                  ok &= Require(point == Point{0, 0} || point == Point{0, 1},
                                "rounded-crossing-has-one-common-node");
                }
              }
            }
            const auto& large_ring = noded[swap ? 1 : 0];
            ok &= Require(std::count(large_ring.begin(), large_ring.end(),
                                     Point{0, 0}) == 2,
                          "both-near-half-crossings-round-to-origin");
          }
        }
      }
    }
  }
  return ok;
}

bool RequireSettledIntegerNoding() {
  const std::vector<Path> paths{
      {{0, 18}, {10, 2}, {15, 13}},
      {{3, 9}, {4, 19}, {9, 4}, {9, 5}},
      {{5, 11}, {7, 7}, {11, 5}},
  };
  const auto noded = NodeToGrid<int64_t>(paths);
  BoundaryNoder<int64_t> fresh(noded);
  const auto checked = fresh.Run();
  bool ok = Require(fresh.AlreadyNoded(), "integer-noding-reaches-fixed-point");
  ok &= Require(checked == noded, "fresh-noder-keeps-settled-output");
  ok &= Require(NodeToGrid<int64_t>(noded) == noded,
                "integer-noding-is-idempotent");
  ok &= Require(noded.size() == paths.size(), "settled-ring-ownership");
  for (std::size_t ring = 0; ring < paths.size(); ++ring) {
    ok &= Require(L1Perimeter(noded[ring]) == L1Perimeter(paths[ring]),
                  "settled-ring-preserves-l1");
  }
  return ok;
}

bool RequireExactRationalRounding() {
  bool ok = true;
  for (int64_t denominator = 1; denominator <= 19; ++denominator) {
    for (int64_t parameter = 0; parameter <= denominator; ++parameter) {
      for (int64_t origin = -5; origin <= 5; ++origin) {
        for (int64_t direction = -9; direction <= 9; ++direction) {
          const int64_t numerator = origin * denominator + direction * parameter;
          const int64_t magnitude = std::abs(numerator);
          const int64_t quotient = magnitude / denominator;
          const int64_t remainder = magnitude % denominator;
          const int64_t rounded = quotient + (remainder >= denominator - remainder);
          const int64_t expected = numerator < 0 ? -rounded : rounded;
          ok &= Require(wide::RoundedCoordinate(origin, direction,
                            wide::Multiply(parameter, 1),
                            wide::Multiply(denominator, 1)) == expected,
                        "small-rational-rounding-oracle");
        }
      }
    }
  }
  const wide::Product two{0, 2};
  const auto positive_half = wide::ScaledProduct(1, wide::Product{0, 1});
  const auto negative_half = positive_half.Negated();
  ok &= Require(!wide::RoundingCellContains(positive_half, two, 0)
                    && wide::RoundingCellContains(positive_half, two, 1),
                "positive-half-cell-ties-away-from-zero");
  ok &= Require(!wide::RoundingCellContains(negative_half, two, 0)
                    && wide::RoundingCellContains(negative_half, two, -1),
                "negative-half-cell-ties-away-from-zero");
  ok &= Require(wide::CertifiedRoundedCoordinate(positive_half, two, 1) == 1,
                "certified-candidate-fast-path");
  ok &= Require(wide::CertifiedRoundedCoordinate(positive_half, two, 0) == 1
                    && wide::CertifiedRoundedCoordinate(negative_half, two, 0) == -1,
                "uncertified-candidate-exact-correction");
  ok &= Require(wide::CertifiedRoundedCoordinate(wide::Signed192{}, two, 1) == 0,
                "exact-zero-correction");

  constexpr int64_t translated = (int64_t{1} << 52) - 2;
  for (const int64_t sign : {-1, 1}) {
    ok &= Require(wide::RoundedCoordinate(sign * translated, sign,
                      wide::Product{0, 1}, two) == sign * (translated + 1),
                  "large-translated-half-tie");
    ok &= Require(wide::RoundedCoordinate(sign * (translated + 1), -sign,
                      wide::Product{0, 1}, two) == sign * (translated + 1),
                  "reversed-large-translated-half-tie");
    const wide::Product large_denominator{int64_t{1} << 42, 1};
    const wide::Product below_half{int64_t{1} << 41, 0};
    ok &= Require(wide::RoundedCoordinate(sign * translated, sign,
                      below_half, large_denominator) == sign * translated,
                  "three-limb-coordinate-cancellation-below-half");
    ok &= Require(wide::RoundedCoordinate(sign * (translated + 1), -sign,
                      below_half, large_denominator) == sign * (translated + 1),
                  "three-limb-coordinate-cancellation-above-half");
  }

  // A floating t rounds to one half on double/extended-double hosts. Exact
  // cancellation still leaves the coordinate strictly below the half cell.
  constexpr int64_t origin = (int64_t{1} << 52) - 1;
  const wide::Product denominator{1, 1};
  const wide::Product parameter{0, (uint64_t{1} << 63) + 1};
  auto numerator = wide::ScaledProduct(origin, denominator);
  numerator.Add(wide::ScaledProduct(-2 * origin + 1, parameter));
  ok &= Require(!wide::RoundingCellContains(numerator, denominator, 1),
                "near-half-floating-estimate-is-uncertified");
  ok &= Require(wide::CertifiedRoundedCoordinate(numerator, denominator, 1) == 0
                    && wide::RoundedCoordinate(origin, -2 * origin + 1,
                                                parameter, denominator) == 0,
                "near-half-rational-corrects-to-zero");
  return ok;
}

long double DistanceToLinework(const Point& point, const Path& path) {
  long double nearest = std::numeric_limits<long double>::infinity();
  for (std::size_t index = 0; index < path.size(); ++index) {
    const auto& start = path[index];
    const auto direction = path[(index + 1) % path.size()] - start;
    const long double length2 = static_cast<long double>(direction.x) * direction.x
        + static_cast<long double>(direction.y) * direction.y;
    if (length2 == 0) continue;
    const auto offset = point - start;
    const long double t = std::clamp(
        (static_cast<long double>(offset.x) * direction.x
         + static_cast<long double>(offset.y) * direction.y) / length2,
        0.0L, 1.0L);
    nearest = std::min(nearest, std::hypot(
        static_cast<long double>(offset.x) - t * direction.x,
        static_cast<long double>(offset.y) - t * direction.y));
  }
  return nearest;
}

bool RequireCumulativeDisplacementBound() {
  const Path original{{8, 2}, {11, 17}, {8, 1}, {13, 14}};
  NodingStats stats;
  const auto noded = NodeToGrid<int64_t>({original}, nullptr, &stats);
  bool ok = Require(noded.size() == 1, "displaced-ring-ownership");
  if (noded.size() != 1) return false;
  const Point displaced{8, 9};
  const auto distance = DistanceToLinework(displaced, original);
  ok &= Require(std::find(noded[0].begin(), noded[0].end(), displaced)
                    != noded[0].end(), "multi-pass-displaced-vertex-retained");
  ok &= Require(std::abs(distance - 21.0L / std::sqrt(234.0L)) < 1e-12L
                    && distance > std::sqrt(0.5L),
                "multi-pass-drift-exceeds-initial-grid-rounding");
  for (const auto& point : noded[0]) {
    ok &= Require(DistanceToLinework(point, original) <= stats.DisplacementBound(),
                  "cumulative-bound-covers-settled-linework");
  }
  NodingStats settled_stats;
  ok &= Require(NodeToGrid<int64_t>(noded, nullptr, &settled_stats) == noded
                    && settled_stats.DisplacementBound() == 0,
                "settled-rerun-adds-no-displacement");
  const std::vector<Path> collinear{
      {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
      {{10, 2}, {15, 2}, {15, 8}, {10, 8}},
  };
  NodeToGrid<int64_t>(collinear, nullptr, &stats);
  ok &= Require(stats.DisplacementBound() == 0,
                "collinear-endpoint-splits-have-zero-displacement");
  const std::vector<Path> point_touch{
      {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
      {{10, 5}, {15, 3}, {15, 7}},
  };
  bool initially_canonical = true;
  const auto shared_vertex = NodeToGrid<int64_t>(
      point_touch, &initially_canonical, &stats);
  ok &= Require(!initially_canonical && stats.DisplacementBound() == 0,
                "canonical-flag-describes-first-input");
  bool settled_canonical = false;
  ok &= Require(NodeToGrid<int64_t>(shared_vertex, &settled_canonical) == shared_vertex
                    && settled_canonical,
                "shared-endpoint-input-is-already-canonical");
  stats.displaced_passes = (uint64_t{1} << 53) + 1;
  ok &= Require(stats.DisplacementBound() == std::ldexp(1.0, 53) + 2,
                "large-integer-displacement-bound-rounds-upward");
  stats.displaced_passes = std::numeric_limits<uint64_t>::max();
  ok &= Require(stats.DisplacementBound() == std::ldexp(1.0, 64),
                "maximum-displacement-count-rounds-upward");
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
  ok &= RequireConsistentRoundedCrossings();
  ok &= RequireSettledIntegerNoding();
  ok &= RequireExactRationalRounding();
  ok &= RequireCumulativeDisplacementBound();
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
