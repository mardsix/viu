export module viu.json;

import std;

import viu.boost;

namespace viu::json {

export struct parser {
public:
    parser(std::stringstream& out) : out_{out} {};

    auto parse(const std::string& data) -> std::string;

    static auto read_u32(const boost::json::value& v) -> std::uint32_t;
    static auto read_u32(
        const boost::json::object& jobject,
        const std::string_view& key
    ) -> std::uint32_t;

private:
    void write_num(std::uint32_t v);
    void write_da(const boost::json::array& a);
    void build_extra(const boost::json::object& jobject);
    void build_endpoint(const boost::json::object& ep);
    void build_interface(const boost::json::object& iface);
    void build_configuration(const boost::json::object& cfg);
    void build_string_descriptors(const boost::json::object& dev);
    void build_bos(const boost::json::object& bos);
    void build_descriptor(const std::string& data);

    std::stringstream& out_;
};

} // namespace viu::json
