#include "cae/geometry.hpp"
#include <gmsh.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>

namespace cae {
namespace {
const double pi = std::acos(-1.0);
void require(bool condition, const std::string &message) {
  if(!condition) throw std::runtime_error(message);
}
double distance(const Point &a, const Point &b) {
  return std::hypot(std::hypot(a[0]-b[0], a[1]-b[1]), a[2]-b[2]);
}
struct Edge {
  std::size_t start = 0, end = 0;
  std::vector<Point> points;
};

BoundaryLoop inspect_loop(const std::vector<int> &curve_tags) {
  require(!curve_tags.empty(), "CAD boundary loop is empty");
  BoundaryLoop loop;
  std::vector<Point> vertices;
  std::vector<Edge> edges;
  auto vertex_index = [&](const Point &p) {
    for(std::size_t i=0; i<vertices.size(); ++i)
      if(distance(p, vertices[i]) <= coordinate_tolerance_m) return i;
    vertices.push_back(p);
    return vertices.size()-1;
  };
  for(int signed_tag : curve_tags) {
    const int tag = std::abs(signed_tag);
    double length = 0;
    gmsh::model::occ::getMass(1, tag, length);
    require(std::isfinite(length) && length > 0, "CAD curve has invalid length");
    loop.length += length;
    std::vector<double> minimum, maximum;
    gmsh::model::getParametrizationBounds(1, tag, minimum, maximum);
    require(minimum.size()==1 && maximum.size()==1, "Invalid CAD curve parameter interval");
    std::string type;
    gmsh::model::getType(1, tag, type);
    const int samples = type == "Line" ? 2 : 65;
    std::vector<double> parameters;
    for(int i=0; i<samples; ++i)
      parameters.push_back(minimum[0] + (maximum[0]-minimum[0])*i/(samples-1));
    std::vector<double> xyz;
    gmsh::model::getValue(1, tag, parameters, xyz);
    require(xyz.size()==3*parameters.size(), "CAD curve sampling returned an unexpected size");
    Edge edge;
    for(std::size_t i=0; i<parameters.size(); ++i) {
      Point point{xyz[3*i], xyz[3*i+1], xyz[3*i+2]};
      require(std::all_of(point.begin(), point.end(), [](double v){return std::isfinite(v);}),
              "CAD curve contains a nonfinite coordinate");
      edge.points.push_back(point);
    }
    edge.start = vertex_index(edge.points.front());
    edge.end = vertex_index(edge.points.back());
    edges.push_back(edge);
  }

  // Endpoint incidence must be two. A periodic circle is a self-loop:
  // its identical start/end vertex receives TWO incidences.
  std::vector<int> degree(vertices.size(), 0);
  for(const auto &edge : edges) { ++degree[edge.start]; ++degree[edge.end]; }
  require(std::all_of(degree.begin(), degree.end(), [](int d){return d==2;}),
          "CAD loop is open or has a branching endpoint");

  // Walk every edge once. Degree two alone does not rule out two disconnected rings.
  std::vector<bool> used(edges.size(), false);
  const auto first_vertex = edges.front().start;
  auto current_vertex = first_vertex;
  for(std::size_t step=0; step<edges.size(); ++step) {
    std::size_t selected = edges.size();
    for(std::size_t i=0; i<edges.size(); ++i)
      if(!used[i] && (edges[i].start==current_vertex || edges[i].end==current_vertex)) {
        selected=i; break;
      }
    require(selected < edges.size(), "CAD loop has disconnected curves");
    auto &edge = edges[selected];
    if(edge.start != current_vertex) std::reverse(edge.points.begin(), edge.points.end());
    const auto begin = edge.points.begin() + (loop.points.empty() ? 0 : 1);
    loop.points.insert(loop.points.end(), begin, edge.points.end());
    current_vertex = edge.start==current_vertex ? edge.end : edge.start;
    used[selected] = true;
  }
  loop.closed = current_vertex==first_vertex &&
                distance(loop.points.front(), loop.points.back()) <= coordinate_tolerance_m;
  require(loop.closed, "CAD loop does not close after traversing all curves");
  return loop;
}
void check_measure(double actual, double expected, const std::string &name) {
  require(std::isfinite(actual) && expected>0 &&
          std::abs(actual-expected) <= measure_relative_tolerance*std::abs(expected),
          name + " differs from the analytic geometry reference");
}
} // namespace

double reference_area(const PlateParameters &p) {
  return p.width*p.height - pi*p.radius*p.radius;
}
double reference_perimeter(const PlateParameters &p) {
  return 2*(p.width+p.height) + 2*pi*p.radius;
}
void validate_parameters(const PlateParameters &p) {
  for(double value : {p.width,p.height,p.radius,p.cx,p.cy})
    require(std::isfinite(value), "All geometry parameters must be finite");
  require(p.width>0 && p.height>0 && p.radius>=coordinate_tolerance_m,
          "Width/height must be positive; radius must be at least 1e-6 m");
  const double clearance = std::min({p.cx-p.radius, p.width-p.cx-p.radius,
                                    p.cy-p.radius, p.height-p.cy-p.radius});
  require(clearance >= coordinate_tolerance_m,
          "Hole must be inside the plate with at least 1e-6 m boundary clearance");
  require(std::isfinite(reference_area(p)) && reference_area(p)>0 &&
          std::isfinite(reference_perimeter(p)), "Geometry parameters overflow the reference measures");
}
void create_plate(const PlateParameters &p) {
  validate_parameters(p);
  gmsh::model::add("perforated_plate");
  const int rectangle = gmsh::model::occ::addRectangle(0, 0, 0, p.width, p.height);
  const int disk = gmsh::model::occ::addDisk(p.cx, p.cy, 0, p.radius, p.radius);
  gmsh::vectorpair result;
  std::vector<gmsh::vectorpair> mapping;
  // dim=2 refers to a CAD FACE. Subtraction returns the new entity tags.
  gmsh::model::occ::cut({{2, rectangle}}, {{2, disk}}, result, mapping);
  require(result.size()==1 && result.front().first==2,
          "Rectangle-minus-disk did not produce one face");
  // Publish OCC changes before querying Gmsh model entities.
  gmsh::model::occ::synchronize();
}
void import_step(const std::filesystem::path &path) {
  gmsh::model::add("reimported_plate");
  gmsh::vectorpair imported;
  gmsh::model::occ::importShapes(path.string(), imported);
  require(!imported.empty(), "STEP import returned no geometry");
  gmsh::model::occ::synchronize();
}
GeometrySnapshot inspect_geometry() {
  GeometrySnapshot snapshot;
  gmsh::vectorpair surfaces, volumes, curves;
  gmsh::model::getEntities(surfaces, 2);
  gmsh::model::getEntities(volumes, 3);
  gmsh::model::getEntities(curves, 1);
  snapshot.surface_count=static_cast<int>(surfaces.size());
  snapshot.volume_count=static_cast<int>(volumes.size());
  snapshot.curve_count=static_cast<int>(curves.size());
  require(surfaces.size()==1 && volumes.empty(), "Expected one planar CAD face and no volumes");
  const int surface_tag = surfaces.front().second;
  gmsh::model::occ::getMass(2, surface_tag, snapshot.area);
  auto &b = snapshot.bounds;
  gmsh::model::getBoundingBox(2, surface_tag, b[0], b[1], b[2], b[3], b[4], b[5]);
  std::vector<int> loop_tags;
  std::vector<std::vector<int>> loop_curves;
  gmsh::model::occ::getCurveLoops(surface_tag, loop_tags, loop_curves);
  require(loop_tags.size()==loop_curves.size(), "OCC loop metadata is inconsistent");
  std::set<int> visited_curves;
  for(const auto &tags : loop_curves) {
    for(int tag : tags)
      require(visited_curves.insert(std::abs(tag)).second, "Curve belongs to multiple boundary loops");
    auto loop = inspect_loop(tags);
    snapshot.perimeter += loop.length;
    snapshot.loops.push_back(loop);
  }
  require(visited_curves.size()==curves.size(), "Geometry contains curves outside the face boundary");
  std::sort(snapshot.loops.begin(), snapshot.loops.end(),
            [](const auto &a, const auto &b){return a.length < b.length;});
  std::vector<std::size_t> node_tags;
  std::vector<double> coordinates, parameters;
  gmsh::model::mesh::getNodes(node_tags, coordinates, parameters);
  snapshot.node_count=node_tags.size();
  std::vector<int> element_types;
  std::vector<std::vector<std::size_t>> element_tags, element_nodes;
  gmsh::model::mesh::getElements(element_types, element_tags, element_nodes);
  for(const auto &tags : element_tags) snapshot.element_count+=tags.size();
  return snapshot;
}
void validate_snapshot(const GeometrySnapshot &s, const PlateParameters &p) {
  require(s.surface_count==1 && s.volume_count==0, "Invalid CAD entity dimensions");
  require(s.node_count==0 && s.element_count==0, "Stage one must contain CAD geometry only");
  require(s.loops.size()==2, "Expected exactly one outer loop and one hole loop");
  for(const auto &loop : s.loops) require(loop.closed, "Boundary loop is not closed");
  check_measure(s.area, reference_area(p), "Face area");
  check_measure(s.perimeter, reference_perimeter(p), "Total boundary length");
  check_measure(s.loops[0].length, 2*pi*p.radius, "Hole circumference");
  check_measure(s.loops[1].length, 2*(p.width+p.height), "Rectangle perimeter");
  // Global area/perimeter cannot distinguish centered and off-center holes.
  // Check sampled geometry against the requested location as well.
  for(const auto &point : s.loops[0].points)
    require(std::abs(std::hypot(point[0]-p.cx, point[1]-p.cy)-p.radius)
              <= coordinate_tolerance_m && std::abs(point[2]) <= coordinate_tolerance_m,
            "Hole boundary differs from the requested center/radius");
  for(const auto &point : s.loops[1].points) {
    const double side_distance = std::min({std::abs(point[0]), std::abs(point[0]-p.width),
                                          std::abs(point[1]), std::abs(point[1]-p.height)});
    require(side_distance <= coordinate_tolerance_m &&
            point[0]>=-coordinate_tolerance_m && point[0]<=p.width+coordinate_tolerance_m &&
            point[1]>=-coordinate_tolerance_m && point[1]<=p.height+coordinate_tolerance_m &&
            std::abs(point[2])<=coordinate_tolerance_m,
            "Outer boundary differs from the requested rectangle");
  }
  const std::array<double,6> expected{0,0,0,p.width,p.height,0};
  for(std::size_t i=0; i<expected.size(); ++i)
    require(std::isfinite(s.bounds[i]) &&
            std::abs(s.bounds[i]-expected[i]) <= coordinate_tolerance_m,
            "CAD bounding box differs from requested plate dimensions");
}
} // namespace cae
