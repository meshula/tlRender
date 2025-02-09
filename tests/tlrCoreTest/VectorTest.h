// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021 Darby Johnston
// All rights reserved.

#pragma once

#include <tlrTestLib/ITest.h>

namespace tlr
{
    namespace CoreTest
    {
        class VectorTest : public Test::ITest
        {
        protected:
            VectorTest(const std::shared_ptr<core::Context>&);

        public:
            static std::shared_ptr<VectorTest> create(const std::shared_ptr<core::Context>&);

            void run() override;
        };
    }
}
