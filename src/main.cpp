#include "cae/geometry.hpp"
#include <gmsh.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace {
struct GmshSession {
  GmshSession() {
    // Ignore ~/.gmshrc so the tutorial is independent of personal GUI settings.
    gmsh::initialize(0, nullptr, false);
    try {
      gmsh::option::setNumber("General.Terminal", 0);
      gmsh::option::setString("Geometry.OCCTargetUnit", "M");
    } catch(...) {
      // The destructor is not invoked if construction fails after initialize.
      try { gmsh::finalize(); } catch(...) {}
      throw;
    }
  }
  ~GmshSession() {
    try { gmsh::finalize(); } catch(...) { /* destructors must not throw */ }
  }
  GmshSession(const GmshSession &) = delete;
  GmshSession &operator=(const GmshSession &) = delete;
};
void print_help() {
  std::cout << "Stage 01: CAD geometry and STEP round trip (all input lengths in m).\n"
    "Usage: geometry_demo [--output NEW_DIRECTORY] [--width W] [--height H]\n"
    "                     [--radius R] [--cx X] [--cy Y]\n"
    "Defaults: W=0.1 H=0.05 R=0.01; hole center defaults to (W/2,H/2).\n"
    "Radius and clearance must be at least 1e-6 m. Existing output is never overwritten.\n";
}
double parse_number(const std::string &text) {
  std::size_t consumed=0;
  const double number=std::stod(text, &consumed);
  if(consumed!=text.size()) throw std::runtime_error("Invalid numeric value: "+text);
  return number;
}
std::map<std::string,std::string> parse_options(int argc, char **argv) {
  std::map<std::string,std::string> options;
  for(int i=1; i<argc; ++i) {
    const std::string key=argv[i];
    if(key!="--output" && key!="--width" && key!="--height" && key!="--radius" &&
       key!="--cx" && key!="--cy") throw std::runtime_error("Unknown option: "+key);
    if(i+1==argc || std::string(argv[i+1]).rfind("--",0)==0)
      throw std::runtime_error("Missing value for "+key);
    if(!options.emplace(key, argv[++i]).second)
      throw std::runtime_error("Duplicate option: "+key);
  }
  return options;
}
void write_snapshot(std::ostream &out, const cae::GeometrySnapshot &s) {
  out << "{\n\"surface_count\":" << s.surface_count
      << ",\"volume_count\":" << s.volume_count << ",\"curve_count\":" << s.curve_count
      << ",\"loop_count\":" << s.loops.size() << ",\"node_count\":" << s.node_count
      << ",\"element_count\":" << s.element_count
      << ",\"closed_loops\":" << (std::all_of(s.loops.begin(),s.loops.end(),
            [](const auto &loop){return loop.closed;}) ? "true" : "false")
      << ",\"area_m2\":" << s.area << ",\"perimeter_m\":" << s.perimeter
      << ",\"bounding_box_m\":[";
  for(std::size_t i=0;i<s.bounds.size();++i) out << (i ? "," : "") << s.bounds[i];
  out << "],\"loops\":[";
  for(std::size_t i=0; i<s.loops.size(); ++i) {
    const auto &loop=s.loops[i];
    out << (i ? "," : "") << "{\"length_m\":" << loop.length << ",\"points_m\":[";
    for(std::size_t j=0;j<loop.points.size();++j) {
      const auto &p=loop.points[j];
      out << (j ? "," : "") << '[' << p[0] << ',' << p[1] << ',' << p[2] << ']';
    }
    out << "]}";
  }
  out << "]\n}";
}
struct StepUnit { std::string name; double to_m; };
StepUnit read_step_unit(const fs::path &path) {
  std::ifstream input(path);
  if(!input) throw std::runtime_error("Cannot read exported STEP file");
  const std::string text((std::istreambuf_iterator<char>(input)), {});
  const std::regex unit_pattern(R"(LENGTH_UNIT\s*\(\s*\)\s+NAMED_UNIT\s*\(\s*\*\s*\)\s+SI_UNIT\s*\(\s*(\$|\.MILLI\.)\s*,\s*\.METRE\.\s*\))");
  std::smatch match;
  if(!std::regex_search(text, match, unit_pattern))
    throw std::runtime_error("STEP length unit is unsupported or missing");
  return match[1].str()==".MILLI." ? StepUnit{"mm",.001} : StepUnit{"m",1.};
}
void write_report(const fs::path &path, const cae::PlateParameters &p,
                  const cae::GeometrySnapshot &before, const cae::GeometrySnapshot &after,
                  const std::string &runtime_version, const StepUnit &unit) {
  std::ofstream out(path);
  if(!out) throw std::runtime_error("Cannot create geometry report");
  out << std::setprecision(17)
      << "{\n\"schema_version\":1,\"stage\":1,\"status\":\"PASS\",\"length_unit\":\"m\",\n"
      << "\"gmsh_header_version\":" << std::quoted(GMSH_API_VERSION)
      << ",\"gmsh_runtime_version\":" << std::quoted(runtime_version)
      << ",\"step_length_unit\":" << std::quoted(unit.name)
      << ",\"step_length_unit_to_m\":" << unit.to_m << ",\n"
      << "\"parameters\":{\"width_m\":" << p.width << ",\"height_m\":" << p.height
      << ",\"radius_m\":" << p.radius << ",\"center_x_m\":" << p.cx << ",\"center_y_m\":" << p.cy << "},\n"
      << "\"reference\":{\"area_m2\":" << cae::reference_area(p)
      << ",\"perimeter_m\":" << cae::reference_perimeter(p) << "},\n"
      << "\"tolerances\":{\"coordinate_m\":" << cae::coordinate_tolerance_m
      << ",\"measure_relative\":" << cae::measure_relative_tolerance << "},\n"
      << "\"before_export\":";
  write_snapshot(out, before);
  out << ",\n\"after_import\":";
  write_snapshot(out, after);
  out << "\n}\n";
  out.close();
  if(!out) throw std::runtime_error("Failed to finish writing the geometry report");
}
int run(int argc, char **argv) {
  if(argc==2 && std::string(argv[1])=="--help") { print_help(); return 0; }
  const auto options=parse_options(argc,argv);
  cae::PlateParameters p;
  auto number=[&](const std::string &key,double fallback) {
    auto found=options.find(key);
    return found==options.end() ? fallback : parse_number(found->second);
  };
  p.width=number("--width",p.width); p.height=number("--height",p.height);
  p.radius=number("--radius",p.radius);
  p.cx=number("--cx",p.width/2); p.cy=number("--cy",p.height/2);
  cae::validate_parameters(p);
  const fs::path output=options.count("--output") ? options.at("--output") : "outputs/stage01";
  if(output.empty()) throw std::runtime_error("Output directory must not be empty");
  if(fs::exists(output) || fs::is_symlink(output))
    throw std::runtime_error("Output path already exists; choose a new --output directory");
  GmshSession session;
  std::string runtime_version;
  gmsh::option::getString("General.Version",runtime_version);
  if(runtime_version!=GMSH_API_VERSION)
    throw std::runtime_error("Gmsh header/runtime mismatch: header "+std::string(GMSH_API_VERSION)+
                             ", runtime "+runtime_version);
  cae::create_plate(p);
  const auto before=cae::inspect_geometry();
  cae::validate_snapshot(before,p);
  // Create the destination only after inputs and initial CAD are valid.
  if(!fs::create_directories(output))
    throw std::runtime_error("Output path appeared during the run; refusing overwrite");
  const auto step=output/"plate.step";
  gmsh::write(step.string());
  const auto unit=read_step_unit(step);
  gmsh::clear(); // The next checks cannot accidentally inspect the original model.
  gmsh::option::setString("Geometry.OCCTargetUnit","M");
  cae::import_step(step);
  const auto after=cae::inspect_geometry();
  cae::validate_snapshot(after,p);
  write_report(output/"geometry_report.json",p,before,after,runtime_version,unit);
  std::cout << std::setprecision(12)
      << "\nPASS: one CAD face, two closed loops, no mesh; STEP round trip verified.\n"
      << "Internal length unit: m; STEP file unit: " << unit.name << '\n'
      << "Area:       " << after.area << " m^2 (reference " << cae::reference_area(p) << ")\n"
      << "Perimeter:  " << after.perimeter << " m (reference " << cae::reference_perimeter(p) << ")\n"
      << "STEP:       " << fs::absolute(step) << '\n'
      << "Report:     " << fs::absolute(output/"geometry_report.json") << '\n';
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  try { return run(argc,argv); }
  catch(const std::exception &error) { std::cerr << "error: " << error.what() << '\n'; }
  catch(const std::string &error) { std::cerr << "error: Gmsh: " << error << '\n'; }
  catch(...) { std::cerr << "error: unknown Gmsh failure\n"; }
  return 1;
}
