export module viu.client;

import std;

import viu.cli;

namespace viu {

export class client {
public:
    auto send_command(int argc, const char* argv[]) -> int;
    auto run(int argc, const char* argv[]) -> int;
};

} // namespace viu
