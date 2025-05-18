module;

#include <boost/describe.hpp>
#include <boost/mp11.hpp>

export module viu.usb.descriptors:traits;

import std;

template <typename T>
struct pointer_to_member_type_helper;

template <typename C, typename T>
struct pointer_to_member_type_helper<T C::*> {
    using type = T;
};

template <typename T>
struct pointer_to_member_type
    : pointer_to_member_type_helper<std::remove_cvref_t<T>> {};

template <typename T>
using member_type_t = pointer_to_member_type<T>::type;

template <typename T>
struct is_pointer_to_non_integral_member {
    using pointer_type = decltype(T::pointer);
    static const bool value = !std::is_integral_v<member_type_t<pointer_type>>;
};

template <typename T>
struct is_described_integral_pod {
    using members =
        boost::describe::describe_members<T, boost::describe::mod_public>;
    using size = boost::mp11::mp_size<members>;
    using result =
        boost::mp11::mp_find_if<members, is_pointer_to_non_integral_member>;

    static const bool value = std::same_as<result, size>;
};

template <typename T>
concept described_integral_pod = requires(T) {
    requires boost::describe::has_describe_members<T>::value;
    requires is_described_integral_pod<T>::value;
};

template <typename T>
concept integral_rv = std::is_integral_v<std::remove_reference_t<T>>;

template <typename T>
concept const_unsigned_char_pointer =
    std::is_same_v<std::remove_reference_t<T>, const unsigned char*>;

template <typename T>
concept descriptor_with_extra = requires(T) {
    described_integral_pod<T>;
    { T::extra_length } -> integral_rv;
    { T::extra } -> const_unsigned_char_pointer;
};
