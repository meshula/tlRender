// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021 Darby Johnston
// All rights reserved.

#include <tlrCore/LogSystem.h>

#include <mutex>

namespace tlr
{
    namespace core
    {
        std::string toString(const LogItem& item)
        {
            std::string out;
            switch (item.type)
            {
            case LogType::Message:
                out = item.prefix + ": " + item.message;
                break;
            case LogType::Warning:
                out = item.prefix + ": Warning: " + item.message;
                break;
            case LogType::Error:
                out = item.prefix + ": ERROR: " + item.message;
                break;
            default: break;
            }
            return out;
        }

        struct LogSystem::Private
        {
            std::shared_ptr<observer::Value<LogItem> > log;
            std::mutex mutex;
        };

        void LogSystem::_init()
        {
            TLR_PRIVATE_P();
            p.log = observer::Value<LogItem>::create();
        }

        LogSystem::LogSystem(const std::shared_ptr<Context>& context) :
            ICoreSystem("tlr::core::LogSystem", context),
            _p(new Private)
        {}

        LogSystem::~LogSystem()
        {}

        std::shared_ptr<LogSystem> LogSystem::create(const std::shared_ptr<Context>& context)
        {
            auto out = std::shared_ptr<LogSystem>(new LogSystem(context));
            out->_init();
            return out;
        }

        void LogSystem::print(
            const std::string& prefix,
            const std::string& value,
            LogType type)
        {
            TLR_PRIVATE_P();
            std::unique_lock<std::mutex> lock(p.mutex);
            p.log->setAlways({ prefix, value, type });
        }

        std::shared_ptr<observer::IValue<LogItem> > LogSystem::observeLog() const
        {
            return _p->log;
        }
    }
}