#pragma once

#include <istream>
#include <string>

#include "thor/models.hpp"

namespace thor {

class Parser {
public:
    Program parse(std::istream& in) const;
    Program parse_text(const std::string& text) const;
};

} // namespace thor
