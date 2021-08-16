/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <libtest/Macros.h> 
#include <base/Format.h>
#include <base/Function.h>
#include <base/NonnullRefPtrVector.h>
#include <base/String.h>
#include <libtest/TestCase.h>

namespace Test {

class TestSuite {
public:
    static TestSuite& the()
    {
        if (s_global == nullptr)
            s_global = new TestSuite();
        return *s_global;
    }

    static void release()
    {
        if (s_global)
            delete s_global;
        s_global = nullptr;
    }

    int run(const NonnullRefPtrVector<TestCase>&);
    int main(const String& suite_name, int argc, char** argv);
    NonnullRefPtrVector<TestCase> find_cases(const String& search, bool find_tests, bool find_benchmarks);
    void add_case(const NonnullRefPtr<TestCase>& test_case)
    {
        m_cases.append(test_case);
    }

    void current_test_case_did_fail() { m_current_test_case_passed = false; }

private:
    static TestSuite* s_global;
    NonnullRefPtrVector<TestCase> m_cases;
    u64 m_testtime = 0;
    u64 m_benchtime = 0;
    String m_suite_name;
    bool m_current_test_case_passed = true;
};

}