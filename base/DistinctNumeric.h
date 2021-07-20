/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

// includes
#include <base/Format.h>
#include <base/Traits.h>
#include <base/Types.h>

namespace Base {

template<typename T, typename X, bool Incr, bool Cmp, bool Bool, bool Flags, bool Shift, bool Arith>
class DistinctNumeric {
    using Self = DistinctNumeric<T, X, Incr, Cmp, Bool, Flags, Shift, Arith>;

public:
    constexpr DistinctNumeric()
    {
    }

    constexpr DistinctNumeric(T value)
        : m_value { value }
    {
    }

    constexpr const T& value() const { return m_value; }

    constexpr bool operator==(const Self& other) const
    {
        return this->m_value == other.m_value;
    }
    constexpr bool operator!=(const Self& other) const
    {
        return this->m_value != other.m_value;
    }

    constexpr Self& operator++()
    {
        static_assert(Incr, "'++a' is only available for DistinctNumeric types with 'Incr'.");
        this->m_value += 1;
        return *this;
    }
    constexpr Self operator++(int)
    {
        static_assert(Incr, "'a++' is only available for DistinctNumeric types with 'Incr'.");
        Self ret = this->m_value;
        this->m_value += 1;
        return ret;
    }
    constexpr Self& operator--()
    {
        static_assert(Incr, "'--a' is only available for DistinctNumeric types with 'Incr'.");
        this->m_value -= 1;
        return *this;
    }
    constexpr Self operator--(int)
    {
        static_assert(Incr, "'a--' is only available for DistinctNumeric types with 'Incr'.");
        Self ret = this->m_value;
        this->m_value -= 1;
        return ret;
    }

    constexpr bool operator>(const Self& other) const
    {
        static_assert(Cmp, "'a>b' is only available for DistinctNumeric types with 'Cmp'.");
        return this->m_value > other.m_value;
    }
    constexpr bool operator<(const Self& other) const
    {
        static_assert(Cmp, "'a<b' is only available for DistinctNumeric types with 'Cmp'.");
        return this->m_value < other.m_value;
    }
    constexpr bool operator>=(const Self& other) const
    {
        static_assert(Cmp, "'a>=b' is only available for DistinctNumeric types with 'Cmp'.");
        return this->m_value >= other.m_value;
    }
    constexpr bool operator<=(const Self& other) const
    {
        static_assert(Cmp, "'a<=b' is only available for DistinctNumeric types with 'Cmp'.");
        return this->m_value <= other.m_value;
    }
    constexpr bool operator!() const
    {
        static_assert(Bool, "'!a' is only available for DistinctNumeric types with 'Bool'.");
        return !this->m_value;
    }


}

}