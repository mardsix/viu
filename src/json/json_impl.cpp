module viu.json;

import std;

import viu.boost;

namespace viu::json {

void parser::write_num(std::uint32_t v) { out_ << v << " "; }

void parser::write_da(const boost::json::array& a)
{
    std::ranges::for_each(a, [&](const auto& v) { write_num(read_u32(v)); });
}

void parser::build_extra(const boost::json::object& jobject)
{
    std::uint32_t total_bytes = 0;

    if (jobject.contains("Endpoint Companion")) {
        total_bytes += 6;
    }

    if (jobject.contains("daExtra")) {
        const auto& extra = jobject.at("daExtra").as_array();
        total_bytes += extra.size();
    }

    if (total_bytes != 0) {
        write_num(total_bytes);

        if (jobject.contains("Endpoint Companion")) {
            const auto& ec = jobject.at("Endpoint Companion").as_object();
            write_num(read_u32(ec, "bLength"));
            write_num(read_u32(ec, "bDescriptorType"));
            write_num(read_u32(ec, "bMaxBurst"));
            write_num(read_u32(ec, "bmAttributes"));
            std::uint16_t wBytes = static_cast<std::uint16_t>(
                read_u32(ec, "wBytesPerInterval")
            );
            write_num(wBytes & 0xFF);
            write_num((wBytes >> 8) & 0xFF);
        }

        if (jobject.contains("daExtra")) {
            const auto& extra = jobject.at("daExtra").as_array();
            write_da(extra);
        }

        return;
    }

    write_num(0);
}

void parser::build_endpoint(const boost::json::object& ep)
{
    const auto& d = ep.at("Endpoint Descriptor").as_object();

    write_num(read_u32(d, "bLength"));
    write_num(read_u32(d, "bDescriptorType"));
    write_num(read_u32(d, "bEndpointAddress"));

    write_num(read_u32(d, "bmAttributes"));
    write_num(read_u32(d, "wMaxPacketSize"));
    write_num(read_u32(d, "bInterval"));
    write_num(read_u32(d, "bRefresh"));
    write_num(read_u32(d, "bSynchAddress"));

    build_extra(ep);
}

void parser::build_interface(const boost::json::object& iface)
{
    const auto& iface_desc = iface.at("Interface Descriptor").as_object();

    write_num(read_u32(iface_desc, "bLength"));
    write_num(read_u32(iface_desc, "bDescriptorType"));
    write_num(read_u32(iface_desc, "bInterfaceNumber"));
    write_num(read_u32(iface_desc, "bAlternateSetting"));
    write_num(read_u32(iface_desc, "bNumEndpoints"));
    write_num(read_u32(iface_desc, "bInterfaceClass"));
    write_num(read_u32(iface_desc, "bInterfaceSubClass"));
    write_num(read_u32(iface_desc, "bInterfaceProtocol"));
    write_num(read_u32(iface_desc, "iInterface"));

    const auto& eps = iface_desc.at("aofEndpoints").as_array();
    write_num(eps.size());
    std::ranges::for_each(eps, [&](const auto& ep) {
        build_endpoint(ep.as_object());
    });

    build_extra(iface_desc);
}

void parser::build_configuration(const boost::json::object& cfg)
{
    const auto& config_desc = cfg.at("Configuration Descriptor").as_object();

    write_num(read_u32(config_desc, "bLength"));
    write_num(read_u32(config_desc, "bDescriptorType"));
    write_num(read_u32(config_desc, "wTotalLength"));
    write_num(read_u32(config_desc, "bNumInterfaces"));
    write_num(read_u32(config_desc, "bConfigurationValue"));
    write_num(read_u32(config_desc, "iConfiguration"));
    write_num(read_u32(config_desc, "bmAttributes"));
    write_num(read_u32(config_desc, "MaxPower"));

    const auto& alts = config_desc.at("aofAltsettings").as_array();
    write_num(alts.size());

    std::ranges::for_each(alts, [&](const auto alt) {
        const auto& ifs = alt.at("aofInterfaces").as_array();
        write_num(ifs.size());
        std::ranges::for_each(ifs, [&](const auto& iface) {
            build_interface(iface.as_object());
        });
    });

    build_extra(cfg);
}

void parser::build_string_descriptors(const boost::json::object& dev)
{
    const auto& langs = dev.at("aofStringDescriptors").as_array();
    write_num(langs.size());

    std::ranges::for_each(langs, [&](const auto& lang) {
        const auto& lang_id = lang.at("wLanguageId");
        if (lang_id.is_array()) {
            write_da(lang_id.as_array());
        } else {
            write_num(read_u32(lang_id));
        }

        const auto& strings = lang.at("aofStrings").as_array();
        write_num(strings.size());

        std::ranges::for_each(strings, [&](const auto& s) {
            const auto& string_desc = s.at("StringDescriptor").as_object();

            // TODO: Remove double length from serialized configurations
            write_num(read_u32(string_desc, "bLength"));
            write_num(read_u32(string_desc, "bLength"));
            write_num(read_u32(string_desc, "bDescriptorType"));

            const auto& str = string_desc.at("string");
            if (str.is_array()) {
                std::ranges::for_each(str.as_array(), [&](const auto& v) {
                    write_num(read_u32(v));
                });
            } else {
                std::ranges::for_each(str.as_string(), [&](char c) {
                    write_num(static_cast<std::uint8_t>(c));
                    write_num(0);
                });
            }
        });
    });
}

void parser::build_bos(const boost::json::object& bos)
{
    write_num(read_u32(bos, "bLength"));
    write_num(read_u32(bos, "bDescriptorType"));
    write_num(read_u32(bos, "wTotalLength"));
    write_num(read_u32(bos, "bNumDeviceCaps"));

    const auto& caps = bos.at("aofDeviceCaps").as_array();
    write_num(caps.size());

    std::ranges::for_each(caps, [&](const auto& cap) {
        const auto& c = cap.as_object();
        write_num(read_u32(c, "bLength"));
        write_num(read_u32(c, "bDescriptorType"));

        auto dev_cap_type = read_u32(c, "bDevCapabilityType");
        write_num(dev_cap_type);

        if (dev_cap_type == 2 && c.contains("USB 2.0 Extension")) {
            const auto& usb2_ext = c.at("USB 2.0 Extension").as_object();
            auto bm_attr = read_u32(usb2_ext, "bmAttributes");
            write_num(4); // TODO: set to packed size of the descriptors
            write_num(bm_attr & 0xFF);
            write_num((bm_attr >> 8) & 0xFF);
            write_num((bm_attr >> 16) & 0xFF);
            write_num((bm_attr >> 24) & 0xFF);
        } else if (dev_cap_type == 3 && c.contains("SuperSpeed USB")) {
            const auto& ss_usb = c.at("SuperSpeed USB").as_object();
            const auto bm_attr = read_u32(ss_usb, "bmAttributes");
            const auto w_speed = read_u32(ss_usb, "wSpeedSupported");
            const auto b_func = read_u32(ss_usb, "bFunctionalitySupport");
            const auto b_u1_lat = read_u32(ss_usb, "bU1DevExitLat");
            const auto w_u2_lat = read_u32(ss_usb, "bU2DevExitLat");

            write_num(7);
            write_num(bm_attr & 0xFF);
            write_num(w_speed & 0xFF);
            write_num((w_speed >> 8) & 0xFF);
            write_num(b_func & 0xFF);
            write_num(b_u1_lat & 0xFF);
            write_num(w_u2_lat & 0xFF);
            write_num((w_u2_lat >> 8) & 0xFF);
        } else if (c.contains("daDevCapability")) {
            const auto& d = c.at("daDevCapability").as_array();
            write_num(d.size());
            write_da(d);
        } else {
            write_num(0);
        }
    });
}

void parser::build_descriptor(const std::string& data)
{
    boost::json::value root = boost::json::parse(data);

    const auto& dev = root.at("aofDevices").as_array()[0].as_object();
    const auto& dd = dev.at("Device Descriptor").as_object();

    write_num(read_u32(dd, "bLength"));
    write_num(read_u32(dd, "bDescriptorType"));
    write_num(read_u32(dd, "bcdUSB"));
    write_num(read_u32(dd, "bDeviceClass"));
    write_num(read_u32(dd, "bDeviceSubClass"));
    write_num(read_u32(dd, "bDeviceProtocol"));
    write_num(read_u32(dd, "bMaxPacketSize0"));
    write_num(read_u32(dd, "idVendor"));
    write_num(read_u32(dd, "idProduct"));
    write_num(read_u32(dd, "bcdDevice"));
    write_num(read_u32(dd, "iManufacturer"));
    write_num(read_u32(dd, "iProduct"));
    write_num(read_u32(dd, "iSerial"));
    write_num(read_u32(dd, "bNumConfigurations"));

    std::ranges::for_each(
        dd.at("aofConfigurations").as_array(),
        [&](const auto& cfg) { build_configuration(cfg.as_object()); }
    );

    build_string_descriptors(dev);

    const auto& report = dev.at("daReportDescriptor").as_array();
    write_num(report.size());
    write_da(report);

    build_bos(dev.at("BOS Descriptor").as_object());
}

auto parser::read_u32(const boost::json::value& v) -> std::uint32_t
{
    if (v.is_int64()) {
        return static_cast<std::uint32_t>(v.as_int64());
    }

    if (v.is_string()) {
        return std::stoul(std::string(v.as_string()), nullptr, 0);
    }

    throw std::runtime_error("Expected int or hex string");
}

auto parser::read_u32(
    const boost::json::object& jobject,
    const std::string_view& key
) -> std::uint32_t
{
    if (jobject.contains(key)) {
        const auto& v = jobject.at(key);
        return read_u32(v);
    }

    return 0;
}

auto parser::parse(const std::string& data) -> std::string
{
    build_descriptor(data);
    return out_.str();
}

} // namespace viu::json
