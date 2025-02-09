// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021 Darby Johnston
// All rights reserved.

#include <tlrCoreTest/PNGTest.h>

#include <tlrCore/AVIOSystem.h>
#include <tlrCore/Assert.h>
#include <tlrCore/PNG.h>

#include <sstream>

namespace tlr
{
    namespace CoreTest
    {
        PNGTest::PNGTest(const std::shared_ptr<core::Context>& context) :
            ITest("CoreTest::PNGTest", context)
        {}

        std::shared_ptr<PNGTest> PNGTest::create(const std::shared_ptr<core::Context>& context)
        {
            return std::shared_ptr<PNGTest>(new PNGTest(context));
        }

        void PNGTest::run()
        {
            auto plugin = _context->getSystem<avio::System>()->getPlugin<png::Plugin>();
            for (const auto& size : std::vector<imaging::Size>(
                {
                    imaging::Size(16, 16),
                    imaging::Size(1, 1),
                    imaging::Size(0, 0)
                }))
            {
                for (const auto& pixelType : plugin->getWritePixelTypes())
                {
                    file::Path path;
                    {
                        std::stringstream ss;
                        ss << "PNGTest_" << size << '_' << pixelType << ".0.png";
                        _print(ss.str());
                        path = file::Path(ss.str());
                    }
                    auto imageInfo = imaging::Info(size, pixelType);
                    imageInfo.layout.alignment = plugin->getWriteAlignment(pixelType);
                    imageInfo.layout.endian = plugin->getWriteEndian();
                    const auto image = imaging::Image::create(imageInfo);
                    try
                    {
                        {
                            avio::Info info;
                            info.video.push_back(imageInfo);
                            info.videoDuration = otime::RationalTime(1.0, 24.0);
                            auto write = plugin->write(path, info);
                            write->writeVideoFrame(otime::RationalTime(0.0, 24.0), image);
                        }
                        auto read = plugin->read(path);
                        const auto videoFrame = read->readVideoFrame(otime::RationalTime(0.0, 24.0)).get();
                    }
                    catch (const std::exception& e)
                    {
                        _printError(e.what());
                    }
                }
            }
        }
    }
}
