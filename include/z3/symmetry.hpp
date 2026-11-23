// Input automorphism group of a Boolean function, computed with bliss.
//
// The function is represented as a vertex-coloured bipartite graph:
//   variable node i -- minterm node m   iff bit i of m is 1
// minterm nodes are coloured by f(m).  Graph automorphisms are exactly the
// input permutations that preserve f.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace dcstudy {

std::vector<std::vector<std::size_t>> bliss_automorphisms(
    std::size_t n, const std::string& truth);

}  // namespace dcstudy
