/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <libjs/runtime/NativeFunction.h>

namespace JS {

class TypedArrayConstructor : public NativeFunction {
    JS_OBJECT(TypedArrayConstructor, NativeFunction);

public:
    TypedArrayConstructor(const FlyString& name, Object& prototype);
    explicit TypedArrayConstructor(GlobalObject&);
    virtual void initialize(GlobalObject&) override;
    virtual ~TypedArrayConstructor() override;

    virtual Value call() override;
    virtual Value construct(FunctionObject& new_target) override;

private:
    virtual bool has_constructor() const override { return true; }

    JS_DECLARE_NATIVE_FUNCTION(from);
    JS_DECLARE_NATIVE_FUNCTION(of);
    JS_DECLARE_NATIVE_GETTER(symbol_species_getter);
};

}
