/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <base/ByteBuffer.h>
#include <base/String.h>
#include <base/StringView.h>

namespace Base {

size_t calculate_base64_decoded_length(const StringView&);

size_t calculate_base64_encoded_length(ReadonlyBytes);

ByteBuffer decode_base64(const StringView&);

String encode_base64(ReadonlyBytes);

}

using Base::decode_base64;
using Base::encode_base64;
