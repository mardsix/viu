#include <gtest/gtest.h>

import std;

import viu.usb.descriptors;

namespace viu::test {

class usb_descriptors_test : public testing::Test {};

TEST_F(usb_descriptors_test, tree)
{
    namespace fs = std::filesystem;
    const auto tmp_config_path = fs::path{
        fs::temp_directory_path() /= "test_device_config-tmp.config"
    };

    auto descriptor_tree_from_json = usb::descriptor::tree{};
    descriptor_tree_from_json.load("test_device_config.json");
    descriptor_tree_from_json.save(tmp_config_path);

    auto descriptor_tree_from_config = usb::descriptor::tree{};
    descriptor_tree_from_config.load(tmp_config_path);

    const auto dd_json = descriptor_tree_from_json.device_descriptor();
    const auto dd_cfg = descriptor_tree_from_config.device_descriptor();

    EXPECT_EQ(dd_json.bLength, dd_cfg.bLength);
    EXPECT_EQ(dd_json.bDescriptorType, dd_cfg.bDescriptorType);
    EXPECT_EQ(dd_json.bcdUSB, dd_cfg.bcdUSB);
    EXPECT_EQ(dd_json.bDeviceClass, dd_cfg.bDeviceClass);
    EXPECT_EQ(dd_json.bDeviceSubClass, dd_cfg.bDeviceSubClass);
    EXPECT_EQ(dd_json.bDeviceProtocol, dd_cfg.bDeviceProtocol);
    EXPECT_EQ(dd_json.bMaxPacketSize0, dd_cfg.bMaxPacketSize0);
    EXPECT_EQ(dd_json.idVendor, dd_cfg.idVendor);
    EXPECT_EQ(dd_json.idProduct, dd_cfg.idProduct);
    EXPECT_EQ(dd_json.bcdDevice, dd_cfg.bcdDevice);
    EXPECT_EQ(dd_json.iManufacturer, dd_cfg.iManufacturer);
    EXPECT_EQ(dd_json.iProduct, dd_cfg.iProduct);
    EXPECT_EQ(dd_json.iSerialNumber, dd_cfg.iSerialNumber);
    EXPECT_EQ(dd_json.bNumConfigurations, dd_cfg.bNumConfigurations);

    usb::descriptor::vector_type bos_json{};
    usb::descriptor::vector_type bos_cfg{};

    // TODO: enable when pack() implementation is visible in the module
    // descriptor_tree_from_json.bos_descriptor().pack(bos_json);
    // descriptor_tree_from_config.bos_descriptor().pack(bos_cfg);

    EXPECT_EQ(bos_json, bos_cfg);

    EXPECT_EQ(
        descriptor_tree_from_json.string_descriptors(),
        descriptor_tree_from_config.string_descriptors()
    );

    EXPECT_EQ(
        descriptor_tree_from_json.report_descriptor(),
        descriptor_tree_from_config.report_descriptor()
    );
}

} // namespace viu::test
