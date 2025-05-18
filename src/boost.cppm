// NOLINTBEGIN(misc-unused-using-decls)
module;

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/asio.hpp>
#include <boost/describe.hpp>
#include <boost/describe/members.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/json.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/mp11.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/optional.hpp>
#include <boost/serialization/string.hpp>
#include <boost/spirit/home/x3.hpp>
#include <boost/system/system_error.hpp>
#include <boost/thread/concurrent_queues/queue_op_status.hpp>
#include <boost/thread/sync_queue.hpp>
#include <boost/type_index.hpp>

#define BOOST_ENABLE_ASSERT_DEBUG_HANDLER
#include <boost/stacktrace.hpp>

export module viu.boost;

namespace boost {

export using boost::lexical_cast;
export using boost::numeric_cast;
export using boost::sync_queue;

} // namespace boost

namespace boost::asio {

export using boost::asio::streambuf;
export using boost::asio::buffer;
export using boost::asio::buffers_begin;
export using boost::asio::buffers_end;
export using boost::asio::read;
export using boost::asio::read_until;
export using boost::asio::write;
export using boost::asio::io_context;
export using boost::asio::transfer_exactly;

} // namespace boost::asio

namespace boost::archive {

export using boost::archive::binary_iarchive;
export using boost::archive::binary_oarchive;

} // namespace boost::archive

namespace boost::serialization {

export using boost::serialization::access;

}

namespace boost::asio::error {

export using boost::asio::error::misc_errors;

}

namespace boost::asio::local {

export using boost::asio::local::stream_protocol;
export using boost::asio::local::connect_pair;

} // namespace boost::asio::local

namespace boost::asio::ip {

export using boost::asio::ip::tcp;

}

namespace boost::endian {

export using boost::endian::native_to_big;
export using boost::endian::big_to_native;
export using boost::endian::little_to_native;
export using boost::endian::native_to_little;
export using boost::endian::load_little_u16;

} // namespace boost::endian

namespace boost::describe {

export using boost::describe::describe_members;
export using boost::describe::enum_to_string;
export using boost::describe::has_describe_members;
export using boost::describe::modifiers;

} // namespace boost::describe

namespace boost::mp11 {

export using boost::mp11::mp_for_each;
export using boost::mp11::mp_find_if;
export using boost::mp11::mp_size;

} // namespace boost::mp11

namespace boost::program_options {

export using boost::program_options::variables_map;
export using boost::program_options::parse_command_line;
export using boost::program_options::store;
export using boost::program_options::notify;
export using boost::program_options::options_description;
export using boost::program_options::value;
export using boost::program_options::unknown_option;

} // namespace boost::program_options

namespace boost::property_tree {

export using boost::property_tree::ptree;
export using boost::property_tree::read_json;

} // namespace boost::property_tree

namespace boost::json {

export using boost::json::array;
export using boost::json::object;
export using boost::json::parse;
export using boost::json::value;

} // namespace boost::json

namespace boost::stacktrace {

export using boost::stacktrace::stacktrace;
export using boost::stacktrace::to_string;
export using boost::stacktrace::operator<<;

} // namespace boost::stacktrace

namespace boost::concurrent {

export using boost::concurrent::sync_queue_is_closed;
export using boost::concurrent::queue_op_status;

} // namespace boost::concurrent

namespace boost::system {

export using boost::system::system_error;
export using boost::system::error_code;
export using boost::system::operator==;

} // namespace boost::system

namespace boost::system::errc {

export using boost::system::errc::errc_t;

}

namespace boost::typeindex {

export using boost::typeindex::operator==;

}

// NOLINTEND(misc-unused-using-decls)
