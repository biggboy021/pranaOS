/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <libjs/runtime/Object.h>

namespace JS {

class BigIntPrototype final : public Object {
    JS_OBJECT(BigIntPrototype, Object);

public:
    explicit BigIntPrototype(GlobalObject&);
    virtual void initialize(GlobalObject&) override;
    virtual ~BigIntPrototype() override;

private:
    JS_DECLARE_NATIVE_FUNCTION(to_string);
    JS_DECLARE_NATIVE_FUNCTION(to_locale_string);
    JS_DECLARE_NATIVE_FUNCTION(value_of);
};

}