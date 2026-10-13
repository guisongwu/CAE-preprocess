#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace cae {
// Every length is in metres; tags belong to the current CAD model only.
struct PlateParameters {
  double width = 0.1;
  double height = 0.05;
  double radius = 0.01;
  double cx = 0.05;
  double cy = 0.025;
};
constexpr double coordinate_tolerance_m = 1e-6;
constexpr double measure_relative_tolerance = 1e-8;
using Point = std::array<double, 3>;
struct BoundaryLoop {
  double length = 0;
  bool closed = false;
  std::vector<Point> points;
};
struct GeometrySnapshot {
  int surface_count = 0;
  int volume_count = 0;
  int curve_count = 0;
  std::size_t node_count = 0;
  std::size_t element_count = 0;
  double area = 0;
  double perimeter = 0;
  std::array<double, 6> bounds{};
  std::vector<BoundaryLoop> loops; // sorted by measured length
};
void validate_parameters(const PlateParameters &p);
void create_plate(const PlateParameters &p);
void import_step(const std::filesystem::path &path);
GeometrySnapshot inspect_geometry();
void validate_snapshot(const GeometrySnapshot &s, const PlateParameters &p);
double reference_area(const PlateParameters &p);
double reference_perimeter(const PlateParameters &p);
} // namespace cae
