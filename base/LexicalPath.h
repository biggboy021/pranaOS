/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

// includes
#include <base/String.h>
#include <base/Vector.h>

namespace AK {

class LexicalPath {
public:
    explicit LexicalPath(String);

    bool is_absolute() const { return !m_string.is_empty() && m_string[0] == '/'; }
    String const& string() const { return m_string; }

    StringView const& dirname() const { return m_dirname; }
    StringView const& basename() const { return m_basename; }
    StringView const& title() const { return m_title; }
    StringView const& extension() const { return m_extension; }

    Vector<StringView> const& parts_view() const { return m_parts; }
    [[nodiscard]] Vector<String> parts() const;

    bool has_extension(StringView const&) const;

    [[nodiscard]] LexicalPath append(StringView const&) const;
    [[nodiscard]] LexicalPath parent() const;

    [[nodiscard]] static String canonicalized_path(String);
    [[nodiscard]] static String relative_path(StringView const& absolute_path, StringView const& prefix);

    template<typename... S>
    [[nodiscard]] static LexicalPath join(String const& first, S&&... rest)
    {
        StringBuilder builder;
        builder.append(first);
        ((builder.append('/'), builder.append(forward<S>(rest))), ...);

        return LexicalPath { builder.to_string() };
    }

    [[nodiscard]] static String dirname(String path)
    {
        auto lexical_path = LexicalPath(move(path));
        return lexical_path.dirname();
    }

}

using Base::LexicalPath;