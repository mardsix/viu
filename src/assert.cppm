export module viu.assert;

import std;

namespace viu {

export void _assert(
    bool exp,
    std::source_location location = std::source_location::current()
);

}
