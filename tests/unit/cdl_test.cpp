#include "helpers.hpp"

TEST_CASE("CDL aliases keep both output ports", "[cdl]") {
  // Two outputs on one net: the exported subckt lists both ports and declares
  // the alias; the analysis must report both, not leave Y1 dangling.
  Subckt c;
  c.name = "ALIAS";
  c.ports = {"A", "B", "Y0", "Y1", "VDD", "VSS"};
  c.pin_dir = {{"A", PinDir::Input},   {"B", PinDir::Input},
               {"Y0", PinDir::Output}, {"Y1", PinDir::Output},
               {"VDD", PinDir::Power}, {"VSS", PinDir::Ground}};
  c.aliases = {{"Y1", "Y0"}};
  auto n = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "NMOS", false};
  };
  auto p = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "PMOS", true};
  };
  c.devices = {n("N1", "Y0", "A", "net"), n("N2", "net", "B", "VSS"),
               p("P1", "Y0", "A", "VDD"), p("P2", "Y0", "B", "VDD")};
  const auto r = analyze_switch(c, {"A", "B"}, {"Y0", "Y1"}, true);
  REQUIRE(r.rows.size() == 4);
  std::string t0, t1;
  for (const auto &row : r.rows) {
    t0.push_back(logic_symbol(row.values[0]));
    t1.push_back(logic_symbol(row.values[1]));
  }
  REQUIRE(t0 == "1110");
  REQUIRE(t1 == "1110");
}

TEST_CASE("CDL parser handles a small subckt", "[cdl]") {
  const auto dir = std::filesystem::temp_directory_path() / "dcstudy-test-cdl";
  std::filesystem::create_directories(dir);
  const auto path = dir / "x.cdl";
  std::ofstream(path) << R"(.SUBCKT INV A Y VDD VSS
*.PININFO A:I Y:O VDD:P VSS:G
M0 Y A VSS VSS NMOS_VTL W=1 L=1
M1 Y A VDD VDD PMOS_VTL W=1 L=1
.ENDS
)";
  const auto lib = CdlLibrary::load(path.string());
  const auto &c = lib.cell("INV");
  REQUIRE(c.transistor_count() == 2);
  REQUIRE(c.inputs() == std::vector<std::string>{"A"});
  REQUIRE(c.outputs() == std::vector<std::string>{"Y"});
  std::filesystem::remove_all(dir);
}
