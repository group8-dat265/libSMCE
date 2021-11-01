/*
 *  test/BoardView.cpp
 *  Copyright 2020-2021 ItJustWorksTM
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include <array>
#include <chrono>
#include <iostream>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "SMCE/Board.hpp"
#include "SMCE/BoardConf.hpp"
#include "SMCE/BoardView.hpp"
#include "SMCE/Toolchain.hpp"
#include "defs.hpp"

using namespace std::literals;

TEST_CASE("BoardView GPIO", "[BoardView]") {
    smce::Toolchain tc{SMCE_PATH};
    REQUIRE(!tc.check_suitable_environment());
    smce::Sketch sk{SKETCHES_PATH "pins", {.fqbn = "arduino:avr:nano"}};
    const auto ec = tc.compile(sk);
    if (ec)
        std::cerr << tc.build_log().second;
    REQUIRE_FALSE(ec);
    smce::Board br{};
    // clang-format off
    smce::BoardConfig bc{
        /* .pins = */{0, 2},
        /* .gpio_drivers = */{
            smce::BoardConfig::GpioDrivers{
                0,
                smce::BoardConfig::GpioDrivers::DigitalDriver{true, false},
                smce::BoardConfig::GpioDrivers::AnalogDriver{true, false}
            },
            smce::BoardConfig::GpioDrivers{
                2,
                smce::BoardConfig::GpioDrivers::DigitalDriver{false, true},
                smce::BoardConfig::GpioDrivers::AnalogDriver{false, true}
            },
        }
    };
    // clang-format on
    REQUIRE(br.configure(std::move(bc)));
    REQUIRE(br.attach_sketch(sk));
    REQUIRE(br.start());
    auto bv = br.view();
    REQUIRE(bv.valid());
    auto pin0 = bv.pins[0];
    REQUIRE(pin0.exists());
    auto pin0d = pin0.digital();
    REQUIRE(pin0d.exists());
    REQUIRE(pin0d.can_read());
    REQUIRE_FALSE(pin0d.can_write());
    auto pin0a = pin0.analog();
    REQUIRE(pin0a.exists());
    REQUIRE(pin0a.can_read());
    REQUIRE_FALSE(pin0a.can_write());
    auto pin1 = bv.pins[1];
    REQUIRE_FALSE(pin1.exists());
    auto pin2 = bv.pins[2];
    REQUIRE(pin2.exists());
    auto pin2d = pin2.digital();
    REQUIRE(pin2d.exists());
    REQUIRE_FALSE(pin2d.can_read());
    REQUIRE(pin2d.can_write());
    auto pin2a = pin2.analog();
    REQUIRE(pin2a.exists());
    REQUIRE_FALSE(pin2a.can_read());
    REQUIRE(pin2a.can_write());
    std::this_thread::sleep_for(1ms);

    pin0d.write(false);
    test_pin_delayable(pin2d, true, 16384, 1ms);
    pin0d.write(true);
    test_pin_delayable(pin2d, false, 16384, 1ms);
    REQUIRE(br.stop());
}

TEST_CASE("BoardView UART", "[BoardView]") {
    smce::Toolchain tc{SMCE_PATH};
    REQUIRE(!tc.check_suitable_environment());
    smce::Sketch sk{SKETCHES_PATH "uart", {.fqbn = "arduino:avr:nano"}};
    const auto ec = tc.compile(sk);
    if (ec)
        std::cerr << tc.build_log().second;
    REQUIRE_FALSE(ec);
    smce::Board br{};
    REQUIRE(br.configure({.uart_channels = {{}}}));
    REQUIRE(br.attach_sketch(sk));
    REQUIRE(br.start());
    auto bv = br.view();
    REQUIRE(bv.valid());
    auto uart0 = bv.uart_channels[0];
    REQUIRE(uart0.exists());
    REQUIRE(uart0.rx().exists());
    REQUIRE(uart0.tx().exists());
    REQUIRE(uart0.rx().max_size() == 64);
    REQUIRE(uart0.tx().max_size() == 64);
    auto uart1 = bv.uart_channels[1];
    REQUIRE_FALSE(uart1.exists());
    REQUIRE_FALSE(uart1.rx().exists());
    REQUIRE_FALSE(uart1.tx().exists());
    REQUIRE_FALSE(uart1.rx().size());
    REQUIRE_FALSE(uart1.tx().size());
    std::this_thread::sleep_for(1ms);

    std::array out = {'H', 'E', 'L', 'L', 'O', ' ', 'U', 'A', 'R', 'T', '\0'};
    std::array<char, out.size()> in{};
    REQUIRE(uart0.rx().front() == '\0');
    REQUIRE(uart0.rx().write(out) == out.size());
    int ticks = 16'000;
    do {
        if (ticks-- == 0)
            FAIL("Timed out");
        std::this_thread::sleep_for(1ms);
    } while (uart0.tx().size() != in.size());
    REQUIRE(uart0.tx().front() == 'H');
    REQUIRE(uart0.tx().read(in) == in.size());
    REQUIRE(uart0.tx().front() == '\0');
    REQUIRE(uart0.tx().size() == 0);
    REQUIRE(in == out);
    REQUIRE_FALSE(uart1.tx().read(in));
    REQUIRE_FALSE(uart1.tx().front());

#if !MSVC_DEBUG
    std::reverse(out.begin(), out.end());
    REQUIRE(uart0.rx().write(out) == out.size());
    ticks = 16'000;
    do {
        if (ticks-- == 0)
            FAIL("Timed out");
        std::this_thread::sleep_for(1ms);
    } while (uart0.tx().size() != in.size());
    REQUIRE(uart0.tx().read(in) == in.size());
    REQUIRE(uart0.tx().size() == 0);
    REQUIRE(in == out);
#endif

    REQUIRE(br.stop());
}

constexpr auto div_ceil(std::size_t lhs, std::size_t rhs) { return lhs / rhs + !!(lhs % rhs); }

constexpr std::byte operator""_b(char c) noexcept { return static_cast<std::byte>(c); }

constexpr std::size_t bpp_444 = 4 + 4 + 4;
constexpr std::size_t bpp_888 = 8 + 8 + 8;

TEST_CASE("BoardView RGB444 cvt", "[BoardView]") {
    smce::Toolchain tc{SMCE_PATH};
    REQUIRE(!tc.check_suitable_environment());
    smce::Sketch sk{SKETCHES_PATH "noop", {.fqbn = "arduino:avr:nano"}};
    const auto ec = tc.compile(sk);
    if (ec)
        std::cerr << tc.build_log().second;
    REQUIRE_FALSE(ec);
    smce::Board br{};
    REQUIRE(br.configure({.frame_buffers = {{}}}));
    REQUIRE(br.attach_sketch(sk));
    REQUIRE(br.prepare());
    auto bv = br.view();
    REQUIRE(bv.valid());
    REQUIRE(br.start());
    REQUIRE(br.suspend());
    auto fb = bv.frame_buffers[0];
    REQUIRE(fb.exists());
    auto fb2 = bv.frame_buffers[1];
    REQUIRE_FALSE(fb2.exists());

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 1;

        constexpr std::array in = {'\xBC'_b, '\x0A'_b};
        constexpr std::array expected_out = {'\xA0'_b, '\xB0'_b, '\xC0'_b};
        REQUIRE(in.size() == expected_out.size() / 3 * 2);

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE(fb.write_rgb444(in));

        std::array<std::byte, std::size(expected_out)> out;
        REQUIRE(fb.read_rgb888(out));
        REQUIRE_FALSE(fb2.read_rgb888(out));
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 2;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\x23'_b, '\xF1'_b, '\x56'_b, '\xF4'_b, '\x89'_b, '\xF7'_b, '\xBC'_b, '\xFA'_b};
        constexpr std::array expected_out = {'\x10'_b, '\x20'_b, '\x30'_b, '\x40'_b, '\x50'_b, '\x60'_b,
                                             '\x70'_b, '\x80'_b, '\x90'_b, '\xA0'_b, '\xB0'_b, '\xC0'_b};
        REQUIRE(in.size() == expected_out.size() / 3 * 2);

        fb.set_height(height);
        fb.set_width(width);
        fb.write_rgb444(in);

        std::array<std::byte, std::size(expected_out)> out;
        fb.read_rgb888(out);
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 1;

        constexpr std::array in = {'\xBC'_b};

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE_FALSE(fb.write_rgb444(in));
    }

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 1;

        constexpr std::array in = {'\xAD'_b, '\xBE'_b, '\xCF'_b};
        constexpr std::array expected_out = {'\xBC'_b, '\x0A'_b};
        REQUIRE(expected_out.size() == in.size() / 3 * 2);

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE(fb.write_rgb888(in));
        REQUIRE_FALSE(fb2.write_rgb888(in));

        std::array<std::byte, std::size(expected_out)> out;
        REQUIRE(fb.read_rgb444(out));
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 2;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\x1A'_b, '\x2B'_b, '\x3C'_b, '\x4D'_b, '\x5E'_b, '\x6F'_b,
                                   '\x7A'_b, '\x8B'_b, '\x9C'_b, '\xAD'_b, '\xBE'_b, '\xCF'_b};
        constexpr std::array expected_out = {'\x23'_b, '\x01'_b, '\x56'_b, '\x04'_b,
                                             '\x89'_b, '\x07'_b, '\xBC'_b, '\x0A'_b};
        REQUIRE(expected_out.size() == in.size() / 3 * 2);

        fb.set_height(height);
        fb.set_width(width);
        fb.write_rgb888(in);

        std::array<std::byte, std::size(expected_out)> out;
        fb.read_rgb444(out);
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 1;

        constexpr std::array in = {'\xAD'_b, '\xBE'_b, '\xCF'_b};

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE(fb.write_rgb888(in));

        std::array<std::byte, 1> out;
        REQUIRE_FALSE(fb.read_rgb444(out));
    }

    REQUIRE(br.resume());
    REQUIRE(br.stop());
}

TEST_CASE("BoardView RGB565 cvt", "[BoardView]") {
    smce::Toolchain tc{SMCE_PATH};
    REQUIRE(!tc.check_suitable_environment());
    smce::Sketch sk{SKETCHES_PATH "noop", {.fqbn = "arduino:avr:nano"}};
    const auto ec = tc.compile(sk);
    if (ec)
        std::cerr << tc.build_log().second;
    REQUIRE_FALSE(ec);
    smce::Board br{};
    REQUIRE(br.configure({.frame_buffers = {{}}}));
    REQUIRE(br.attach_sketch(sk));
    REQUIRE(br.prepare());
    auto bv = br.view();
    REQUIRE(bv.valid());
    REQUIRE(br.start());
    REQUIRE(br.suspend());
    auto fb = bv.frame_buffers[0];
    REQUIRE(fb.exists());
    auto fb2 = bv.frame_buffers[1];
    REQUIRE_FALSE(fb2.exists());

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 1;

        constexpr std::array in = {'\xBC'_b, '\x0A'_b};
        constexpr std::array expected_out = {'\xB8'_b, '\x80'_b, '\x50'_b};
        REQUIRE(in.size() == expected_out.size() / 3 * 2);

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE(fb.write_rgb565(in));
        REQUIRE_FALSE(fb2.write_rgb565(in));

        std::array<std::byte, std::size(expected_out)> out;
        REQUIRE(fb.read_rgb888(out));
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 2;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\x23'_b, '\xF1'_b, '\x56'_b, '\xF4'_b, '\x89'_b, '\xF7'_b, '\xBC'_b, '\xFA'_b};
        constexpr std::array expected_out = {'\x20'_b, '\x7C'_b, '\x88'_b, '\x50'_b, '\xDC'_b, '\xA0'_b,
                                             '\x88'_b, '\x3C'_b, '\xB8'_b, '\xB8'_b, '\x9C'_b, '\xD0'_b};
        REQUIRE(in.size() == expected_out.size() / 3 * 2);

        fb.set_height(height);
        fb.set_width(width);
        fb.write_rgb565(in);

        std::array<std::byte, std::size(expected_out)> out;
        fb.read_rgb888(out);
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 1;

        constexpr std::array in = {'\xBC'_b};

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE_FALSE(fb.write_rgb565(in));
    }

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 1;

        constexpr std::array in = {'\xAD'_b, '\xBE'_b, '\xCF'_b};
        constexpr std::array expected_out = {'\xAD'_b, '\xD9'_b};
        REQUIRE(expected_out.size() == in.size() / 3 * 2);

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE(fb.write_rgb888(in));

        std::array<std::byte, std::size(expected_out)> out;
        REQUIRE(fb.read_rgb565(out));
        REQUIRE_FALSE(fb2.read_rgb565(out));
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 2;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\x1A'_b, '\x2B'_b, '\x3C'_b, '\x4D'_b, '\x5E'_b, '\x6F'_b,
                                   '\x7A'_b, '\x8B'_b, '\x9C'_b, '\xAD'_b, '\xBE'_b, '\xCF'_b};
        constexpr std::array expected_out = {'\x19'_b, '\x67'_b, '\x4A'_b, '\xCD'_b,
                                             '\x7C'_b, '\x73'_b, '\xAD'_b, '\xD9'_b};
        REQUIRE(expected_out.size() == in.size() / 3 * 2);

        fb.set_height(height);
        fb.set_width(width);
        fb.write_rgb888(in);

        std::array<std::byte, std::size(expected_out)> out;
        fb.read_rgb565(out);
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 1;

        constexpr std::array in = {'\xAD'_b, '\xBE'_b, '\xCF'_b};

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE(fb.write_rgb888(in));

        std::array<std::byte, 1> out;
        REQUIRE_FALSE(fb.read_rgb565(out));
    }

    REQUIRE(br.resume());
    REQUIRE(br.stop());
}

TEST_CASE("BoardView YUV422 cvt", "[BoardView]") {
    smce::Toolchain tc{SMCE_PATH};
    REQUIRE(!tc.check_suitable_environment());
    smce::Sketch sk{SKETCHES_PATH "noop", {.fqbn = "arduino:avr:nano"}};
    const auto ec = tc.compile(sk);
    if (ec)
        std::cerr << tc.build_log().second;
    REQUIRE_FALSE(ec);
    smce::Board br{};
    REQUIRE(br.configure({.frame_buffers = {{}}}));
    REQUIRE(br.attach_sketch(sk));
    REQUIRE(br.prepare());
    auto bv = br.view();
    REQUIRE(bv.valid());
    REQUIRE(br.start());
    REQUIRE(br.suspend());
    auto fb = bv.frame_buffers[0];
    REQUIRE(fb.exists());
    auto fb2 = bv.frame_buffers[1];
    REQUIRE_FALSE(fb2.exists());

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\xBC'_b, '\x0A'_b, '\xAB'_b, '\x1F'_b};
        constexpr std::array expected_out = {'\x3E'_b, '\x00'_b, '\x72'_b, '\x56'_b, '\x00'_b, '\x8A'_b};
        REQUIRE(in.size() == expected_out.size() / 6 * 4);

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE(fb.write_yuv422(in));
        REQUIRE_FALSE(fb2.write_yuv422(in));

        std::array<std::byte, std::size(expected_out)> out;
        REQUIRE(fb.read_rgb888(out));
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 2;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\x23'_b, '\xF1'_b, '\x56'_b, '\xF4'_b, '\x89'_b, '\xF7'_b, '\xBC'_b, '\xFA'_b};
        constexpr std::array expected_out = {'\xC3'_b, '\xFF'_b, '\x4A'_b, '\xC6'_b, '\xFF'_b, '\x4E'_b,
                                             '\xFF'_b, '\xD9'_b, '\xFF'_b, '\xFF'_b, '\xDC'_b, '\xFF'_b};
        REQUIRE(in.size() == expected_out.size() / 6 * 4);

        fb.set_height(height);
        fb.set_width(width);
        fb.write_yuv422(in);

        std::array<std::byte, std::size(expected_out)> out;
        fb.read_rgb888(out);
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\xBC'_b, '\x0A'_b, '\xAB'_b};

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE_FALSE(fb.write_yuv422(in));
    }

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\xAD'_b, '\xD9'_b, '\xAB'_b, '\xD8'_b, '\xAB'_b, '\xD6'_b};
        constexpr std::array expected_out = {'\x7F'_b, '\xBA'_b, '\x80'_b, '\xB2'_b};
        REQUIRE(expected_out.size() == in.size() / 6 * 4);

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE(fb.write_rgb888(in));

        std::array<std::byte, std::size(expected_out)> out;
        REQUIRE(fb.read_yuv422(out));
        REQUIRE_FALSE(fb2.read_yuv422(out));
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 2;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\x1A'_b, '\x2B'_b, '\x3C'_b, '\x4D'_b, '\x5E'_b, '\x6F'_b,
                                   '\x7A'_b, '\x8B'_b, '\x9C'_b, '\xAD'_b, '\xBE'_b, '\xCF'_b};
        constexpr std::array expected_out = {'\x89'_b, '\x32'_b, '\x77'_b, '\x5E'_b,
                                             '\x89'_b, '\x84'_b, '\x77'_b, '\xB0'_b};
        REQUIRE(expected_out.size() == in.size() / 6 * 4);

        fb.set_height(height);
        fb.set_width(width);
        fb.write_rgb888(in);

        std::array<std::byte, std::size(expected_out)> out;
        fb.read_yuv422(out);
        REQUIRE(out == expected_out);
    }

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 2;

        constexpr std::array in = {'\xAD'_b, '\xD9'_b, '\xAB'_b, '\xD8'_b, '\xAB'_b, '\xD6'_b};

        fb.set_height(height);
        fb.set_width(width);
        REQUIRE(fb.write_rgb888(in));

        std::array<std::byte, 1> out;
        REQUIRE_FALSE(fb.read_yuv422(out));
    }

    REQUIRE(br.resume());
    REQUIRE(br.stop());
}

TEST_CASE("BoardView FrameBuffer others", "[BoardView]") {
    smce::Toolchain tc{SMCE_PATH};
    REQUIRE(!tc.check_suitable_environment());
    smce::Sketch sk{SKETCHES_PATH "noop", {.fqbn = "arduino:avr:nano"}};
    const auto ec = tc.compile(sk);
    if (ec)
        std::cerr << tc.build_log().second;
    REQUIRE_FALSE(ec);
    smce::Board br{};
    REQUIRE(br.configure({.frame_buffers = {{}}}));
    REQUIRE(br.attach_sketch(sk));
    REQUIRE(br.prepare());
    auto bv = br.view();
    REQUIRE(bv.valid());
    REQUIRE(br.start());
    REQUIRE(br.suspend());
    auto fb = bv.frame_buffers[0];
    REQUIRE(fb.exists());
    auto fb2 = bv.frame_buffers[1];
    REQUIRE_FALSE(fb2.exists());

    {
        constexpr std::size_t height = 1;
        constexpr std::size_t width = 1;

        fb.set_height(height);
        fb.set_width(width);
        fb2.set_height(height);
        fb2.set_width(width);

        REQUIRE(fb.get_height() == height);
        REQUIRE_FALSE(fb2.get_height());
        REQUIRE(fb.get_width() == width);
        REQUIRE_FALSE(fb2.get_width());

        fb.needs_vertical_flip(true);
        REQUIRE(fb.needs_vertical_flip());
        REQUIRE_FALSE(fb2.needs_vertical_flip());
        fb.needs_horizontal_flip(true);
        REQUIRE(fb.needs_horizontal_flip());
        REQUIRE_FALSE(fb2.needs_horizontal_flip());

        // TODO Check what a real freq could be
        REQUIRE(fb.get_freq() == 0);
        fb.set_freq(5);
        fb2.set_freq(5);
        REQUIRE(fb.get_freq() == 5);
    }

    REQUIRE(br.resume());
    REQUIRE(br.stop());
}
