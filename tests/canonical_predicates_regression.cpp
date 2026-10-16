// Release-build regression for the integer canonical-boundary predicates.
// This file includes the production header directly; it must never copy the
// predicate implementations into a test-only substitute.
#include "canonical_boundary.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
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

bool RequireBoundedOriginalLineworkNoding(int64_t height) {
  const Path original{Point{int64_t{0}, int64_t{1}}, Point{int64_t{3}, height},
                      Point{int64_t{0}, int64_t{0}},
                      Point{int64_t{5}, height * 4 / 5}};
  NodingStats stats;
  const auto noded = NodeToGrid<int64_t>({original}, nullptr, &stats);
  if (!Require(noded.size() == 1, "bounded-routing-ring-ownership")) return false;
  // Four original segments and five rounded contact/endpoint centers give
  // this arrangement a finite bound independent of the coordinate span.
  if (noded[0].size() > 20) {
    std::cerr << "FAIL original-linework-arrangement-bound: "
              << noded[0].size() << " vertices, expected at most 20\n";
    return false;
  }
  for (const auto& point : original) {
    if (!Require(std::find(noded[0].begin(), noded[0].end(), point)
                     != noded[0].end(), "bounded-routing-keeps-original-vertices")) {
      return false;
    }
  }
  BoundaryNoder<int64_t> fresh(noded);
  const auto checked = fresh.Run();
  bool ok = Require(fresh.AlreadyNoded(), "bounded-routing-is-fully-noded");
  ok &= Require(checked == noded, "bounded-routing-fresh-audit-is-unchanged");
  ok &= Require(NodeToGrid<int64_t>(noded) == noded,
                "bounded-routing-is-idempotent");
  ok &= Require(stats.DisplacementBound() <= 1,
                "bounded-routing-original-grid-error");
  for (const auto& point : noded[0]) {
    ok &= Require(DistanceToLinework(point, original) <= stats.DisplacementBound(),
                  "bounded-routing-vertices-stay-by-original-linework");
  }
  return ok;
}

bool RequireCumulativeDisplacementBound() {
  const Path original{{8, 2}, {11, 17}, {8, 1}, {13, 14}};
  NodingStats stats;
  const auto noded = NodeToGrid<int64_t>({original}, nullptr, &stats);
  bool ok = Require(noded.size() == 1, "displaced-ring-ownership");
  if (noded.size() != 1) return false;
  ok &= Require(stats.DisplacementBound() <= 1,
                "routing-has-one-original-linework-bound");
  for (const auto& point : noded[0]) {
    ok &= Require(DistanceToLinework(point, original) <= stats.DisplacementBound(),
                  "original-bound-covers-every-routed-vertex");
  }
  BoundaryNoder<int64_t> fresh(noded);
  ok &= Require(fresh.Run() == noded && fresh.AlreadyNoded(),
                "original-bound-linework-is-fully-noded");
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
  for (int power = 54; power <= 63; ++power) {
    stats.displaced_passes = (uint64_t{1} << power) + 1;
    const auto exact_power = std::ldexp(1.0, power);
    ok &= Require(stats.DisplacementBound() == std::nextafter(
                      exact_power, std::numeric_limits<double>::infinity()),
                  "displacement-above-large-power-rounds-upward");
    stats.displaced_passes = uint64_t{1} << power;
    ok &= Require(stats.DisplacementBound() == exact_power,
                  "exact-power-displacement-bound-is-unchanged");
  }
  stats.displaced_passes = std::numeric_limits<uint64_t>::max();
  ok &= Require(stats.DisplacementBound() == std::ldexp(1.0, 64),
                "maximum-displacement-count-rounds-upward");
  return ok;
}

Path LeastRotation(const Path& path) {
  Path least = path;
  Path rotated = path;
  const auto before = [](const Point& a, const Point& b) {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
  };
  for (std::size_t start = 1; start < path.size(); ++start) {
    std::rotate(rotated.begin(), rotated.begin() + 1, rotated.end());
    if (std::lexicographical_compare(rotated.begin(), rotated.end(),
                                     least.begin(), least.end(), before)) {
      least = rotated;
    }
  }
  return least;
}

bool RequireCellOwnership() {
  struct Example {
    Point start;
    Point end;
    Point center;
    bool meets;
    const char* label;
  };
  constexpr int64_t large = (int64_t{1} << 52) - 2;
  const std::vector<Example> examples{
      {{0, 1}, {1, 0}, {1, 1}, true, "positive-half-corner-is-owned"},
      {{0, 1}, {1, 0}, {0, 0}, false, "zero-positive-half-corner-is-open"},
      {{0, -1}, {-1, 0}, {-1, -1}, true, "negative-half-corner-is-owned"},
      {{0, -1}, {-1, 0}, {0, 0}, false, "zero-negative-half-corner-is-open"},
      {{0, -1}, {1, 0}, {1, -1}, true, "mixed-half-corner-is-owned"},
      {{0, -1}, {1, 0}, {0, 0}, false, "mixed-zero-corner-is-open"},
      {{1, 2}, {2, 1}, {1, 1}, false, "positive-upper-half-corner-is-open"},
      {{-1, -2}, {-2, -1}, {-1, -1}, false,
       "negative-lower-half-corner-is-open"},
      {{0, -5}, {0, 5}, {0, 2}, true, "vertical-axis-visits-zero-column"},
      {{0, -5}, {0, 5}, {1, 2}, false, "vertical-axis-misses-neighbor-column"},
      {{-5, 0}, {5, 0}, {-2, 0}, true, "horizontal-axis-visits-zero-row"},
      {{-5, 0}, {5, 0}, {-2, -1}, false,
       "horizontal-axis-misses-neighbor-row"},
      {{0, 0}, {0, 0}, {0, 0}, true, "closed-segment-endpoint-is-owned"},
      {{0, 0}, {0, 0}, {0, 1}, false, "point-misses-other-cell"},
      {Point{large - 1, large}, Point{large, large - 1}, Point{large, large},
       true, "large-positive-half-cell"},
      {Point{-large + 1, -large}, Point{-large, -large + 1},
       Point{-large, -large}, true, "large-negative-half-cell"},
      {Point{-large, large + 1}, Point{large + 1, -large}, {1, 1},
       true, "wide-fraction-owned-corner"},
      {Point{-large, large + 1}, Point{large + 1, -large}, {0, 0},
       false, "wide-fraction-open-corner"},
  };
  bool ok = true;
  for (const auto& example : examples) {
    ok &= Require(GridRoundingCellMeetsSegment(
                      example.start, example.end, example.center) == example.meets,
                  example.label);
    ok &= Require(GridRoundingCellMeetsSegment(
                      example.end, example.start, example.center) == example.meets,
                  "cell-ownership-is-reversal-invariant");
  }
  return ok;
}

bool RequireRoutingContract(const std::vector<Path>& original,
                            const std::vector<Path>& noded,
                            const NodingStats& stats) {
  if (!Require(noded.size() == original.size(), "routing-retains-ring-ownership")) {
    return false;
  }
  BoundaryNoder<int64_t> fresh(noded);
  bool ok = Require(fresh.Run() == noded && fresh.AlreadyNoded(),
                    "routing-output-is-fully-noded");
  ok &= Require(NodeToGrid<int64_t>(noded) == noded,
                "routing-output-is-idempotent");
  ok &= Require(stats.DisplacementBound() <= 1, "routing-bound-never-accumulates");
  for (std::size_t ring = 0; ring < original.size(); ++ring) {
    const bool has_segment = !original[ring].empty() && std::any_of(
        original[ring].begin(), original[ring].end(),
        [&](const Point& point) { return point != original[ring].front(); });
    if (has_segment) {
      for (const auto& point : original[ring]) {
        ok &= Require(std::find(noded[ring].begin(), noded[ring].end(), point)
                          != noded[ring].end(),
                      "routing-keeps-every-original-nonzero-edge-vertex");
      }
    }
    for (const auto& point : noded[ring]) {
      if (stats.DisplacementBound() != 0) {
        ok &= Require(DistanceToLinework(point, original[ring])
                          <= stats.DisplacementBound(),
                      "routing-bound-is-measured-from-original-ring");
      } else {
        bool on_original = false;
        for (std::size_t edge = 0; edge < original[ring].size(); ++edge) {
          const auto& a = original[ring][edge];
          const auto& b = original[ring][(edge + 1) % original[ring].size()];
          on_original |= Domain<int64_t>::CrossIsZero(b - a, point - a)
              && point.x >= std::min(a.x, b.x) && point.x <= std::max(a.x, b.x)
              && point.y >= std::min(a.y, b.y) && point.y <= std::max(a.y, b.y);
        }
        ok &= Require(on_original, "zero-routing-bound-means-exact-linework");
      }
    }
  }
  return ok;
}

bool RequireRoutingSymmetries(const std::vector<Path>& original) {
  NodingStats stats;
  const auto expected = NodeToGrid<int64_t>(original, nullptr, &stats);
  if (!RequireRoutingContract(original, expected, stats)) return false;
  // Reverse and rotate each ring independently, and reverse ring order. Every
  // transformed output must still describe the same directed cyclic linework.
  for (std::size_t changed = 0; changed < original.size(); ++changed) {
    auto variant = original;
    auto& path = variant[changed];
    std::reverse(path.begin(), path.end());
    if (!path.empty()) std::rotate(path.begin(), path.begin() + path.size() / 2,
                                   path.end());
    std::reverse(variant.begin(), variant.end());
    const auto actual = NodeToGrid<int64_t>(variant, nullptr, &stats);
    if (!RequireRoutingContract(variant, actual, stats)) return false;
    for (std::size_t ring = 0; ring < original.size(); ++ring) {
      auto transformed = actual[original.size() - 1 - ring];
      if (ring == changed) std::reverse(transformed.begin(), transformed.end());
      if (!Require(LeastRotation(transformed) == LeastRotation(expected[ring]),
                   "routing-reversal-start-and-permutation-invariance")) {
        return false;
      }
    }
  }
  return true;
}

bool RequireAllSmallArrangementOrderings(const std::vector<Path>& originals) {
  const auto expected = NodeToGrid<int64_t>(originals);
  for (std::size_t ring = 0; ring < originals.size(); ++ring) {
    for (std::size_t start = 0; start < originals[ring].size(); ++start) {
      for (const bool reverse : {false, true}) {
        auto variant = originals;
        auto& path = variant[ring];
        std::rotate(path.begin(), path.begin() + start, path.end());
        if (reverse) std::reverse(path.begin(), path.end());
        auto actual = NodeToGrid<int64_t>(variant);
        if (reverse) std::reverse(actual[ring].begin(), actual[ring].end());
        for (std::size_t other = 0; other < originals.size(); ++other) {
          if (!Require(LeastRotation(actual[other]) == LeastRotation(expected[other]),
                       "every-start-and-orientation-preserves-routing")) return false;
        }
      }
    }
  }
  std::vector<std::size_t> order(originals.size());
  std::iota(order.begin(), order.end(), 0);
  do {
    std::vector<Path> variant;
    for (const auto ring : order) variant.push_back(originals[ring]);
    const auto actual = NodeToGrid<int64_t>(variant);
    for (std::size_t ring = 0; ring < originals.size(); ++ring) {
      if (!Require(LeastRotation(actual[ring]) == LeastRotation(expected[order[ring]]),
                   "every-ring-permutation-preserves-routing")) return false;
    }
  } while (std::next_permutation(order.begin(), order.end()));
  return true;
}

bool RequireLocalOriginalContactSets() {
  const Path a{{0, 0}, {1, 0}, {1, -1}, {0, -1}};
  const Path b{{-1, 0}, {999, 1}, {-1, 2}};
  const Path remote{{10000, 10001}, {10003, 10200}, {10000, 10000},
                    {10005, 10160}};
  const std::vector<Path> paths{a, b, remote};
  NodingStats stats;
  const auto noded = NodeToGrid<int64_t>(paths, nullptr, &stats);
  bool ok = RequireRoutingContract(paths, noded, stats);
  ok &= Require(noded[0] == a && noded[1] == b,
                "disjoint-subgrid-gap-is-not-attracted-by-remote-routing");
  ok &= Require(SignedArea<int64_t>(noded[0]) == -1
                    && SignedArea<int64_t>(noded[1]) == 1000,
                "disjoint-subgrid-gap-keeps-exact-coverage");

  const Path shell{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
  const Path hole{{20, 20}, {20, 80}, {80, 80}, {80, 20}};
  const Path independent{{200, 0}, {240, 0}, {240, 40}, {200, 40}};
  const std::vector<Path> nested{shell, hole, independent, remote};
  const auto routed_nested = NodeToGrid<int64_t>(nested);
  ok &= Require(routed_nested[0] == shell && routed_nested[1] == hole
                    && routed_nested[2] == independent,
                "containment-does-not-form-a-computational-contact-set");
  CanonicalNode<int64_t> tree;
  BuildFacesFromNoded<int64_t>(routed_nested, tree);
  bool found_shell = false;
  bool found_independent = false;
  for (const auto& child : tree.children) {
    if (SignedArea<int64_t>(child->path) == 10000) {
      found_shell = !child->is_hole && child->children.size() == 1
          && child->children[0]->is_hole
          && SignedArea<int64_t>(child->children[0]->path) == -3600;
    }
    if (SignedArea<int64_t>(child->path) == 1600) {
      found_independent = !child->is_hole && child->children.empty();
    }
  }
  ok &= Require(found_shell && found_independent,
                "coarse-shell-hole-and-independent-shell-keep-exact-areas");
  ok &= RequireRoutingSymmetries(paths);
  ok &= RequireAllSmallArrangementOrderings(paths);
  return ok;
}

bool RequireActualContactFeedbackJoin() {
  const std::vector<Path> originals{
      {{0, 1}, {3, 20}, {0, 0}, {5, 16}},
      {{1, 8}, {0, 8}, {0, 9}},
  };
  NodingContactSets sets(originals.size());
  BoundaryNoder<int64_t> first_audit(originals, &sets);
  const auto original_cuts = first_audit.Run();
  bool ok = Require(sets.Find(0) != sets.Find(1),
                    "feedback-original-linework-is-exactly-disjoint");
  auto first_routes = original_cuts;
  RouteDirtyOriginalGridSets(originals, original_cuts, sets, first_routes);
  // The routed (0,2)->(3,20) segment meets the independent triangle's existing
  // endpoint (1,8). This actual contact, not cell proximity, joins the sets.
  ok &= Require(std::find(first_routes[0].begin(), first_routes[0].end(),
                          Point{1, 8}) == first_routes[0].end(),
                "feedback-first-route-has-not-already-added-contact");
  const auto initial_joins = sets.Joins();
  BoundaryNoder<int64_t> contact_audit(first_routes, &sets);
  contact_audit.Run();
  ok &= Require(!contact_audit.AlreadyNoded()
                    && sets.Joins() == initial_joins + 1
                    && sets.Find(0) == sets.Find(1),
                "feedback-joins-only-on-actual-routed-contact");
  NodingStats stats;
  const auto result = NodeToGrid<int64_t>(originals, nullptr, &stats);
  ok &= RequireRoutingContract(originals, result, stats);
  ok &= Require(std::find(result[0].begin(), result[0].end(), Point{1, 8})
                    != result[0].end(),
                "feedback-rebuild-nodes-the-new-contact");
  ok &= RequireRoutingSymmetries(originals);
  ok &= RequireAllSmallArrangementOrderings(originals);
  return ok;
}

bool RequireRecordedSeedRoutingFuzz() {
  constexpr uint64_t seed = 0x4c554d494e415352ULL;
  std::mt19937_64 random(seed);
  for (std::size_t sample = 0; sample < 5000; ++sample) {
    std::vector<Path> paths(1 + random() % 4);
    for (auto& path : paths) {
      const auto size = 3 + random() % 4;
      for (std::size_t vertex = 0; vertex < size; ++vertex) {
        const auto x = static_cast<int64_t>(random() % 25) - 12;
        const auto y = static_cast<int64_t>(random() % 25) - 12;
        path.emplace_back(x, y);
      }
    }
    try {
      if (RequireRoutingSymmetries(paths)) continue;
    } catch (const std::exception& error) {
      std::cerr << "routing-fuzz exception=" << error.what() << '\n';
    }
    std::cerr << "routing-fuzz seed=" << seed << " sample=" << sample << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  if (!RequireBoundedOriginalLineworkNoding(200)) return 1;
  if (!RequireBoundedOriginalLineworkNoding(200'000'000'000LL)) return 1;
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
  ok &= RequireCellOwnership();
  ok &= RequireLocalOriginalContactSets();
  ok &= RequireActualContactFeedbackJoin();
  ok &= RequireRoutingSymmetries({Path{{0, 1}, {3, 200}, {0, 0}, {5, 160}}});
  ok &= RequireAllSmallArrangementOrderings(
      {Path{{0, 1}, {3, 200}, {0, 0}, {5, 160}}});
  ok &= RequireRecordedSeedRoutingFuzz();
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
