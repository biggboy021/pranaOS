/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/NonnullRefPtr.h>
#include <base/RefCounted.h>
#include <base/String.h>
#include <base/Types.h>
#include <base/Vector.h>

namespace JS {
namespace Detail {

class Utf16StringImpl : public RefCounted<Utf16StringImpl> {
public:
    ~Utf16StringImpl() = default;

    static NonnullRefPtr<Utf16StringImpl> create();
    static NonnullRefPtr<Utf16StringImpl> create(Vector<u16>);
    static NonnullRefPtr<Utf16StringImpl> create(StringView const&);
    static NonnullRefPtr<Utf16StringImpl> create(Utf16View const&);

    Vector<u16> const& string() const;
    Utf16View view() const;

private:
    Utf16StringImpl() = default;
    explicit Utf16StringImpl(Vector<u16> string);

    Vector<u16> m_string;
};

}

class Utf16String {
public:
    Utf16String();
    explicit Utf16String(Vector<u16>);
    explicit Utf16String(StringView const&);
    explicit Utf16String(Utf16View const&);

    Vector<u16> const& string() const;
    Utf16View view() const;
    Utf16View substring_view(size_t code_unit_offset, size_t code_unit_length) const;
    Utf16View substring_view(size_t code_unit_offset) const;

    String to_utf8() const;
    u16 code_unit_at(size_t index) const;

    size_t length_in_code_units() const;
    bool is_empty() const;

private:
    NonnullRefPtr<Detail::Utf16StringImpl> m_string;
};

}
