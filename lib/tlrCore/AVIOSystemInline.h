// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021 Darby Johnston
// All rights reserved.

namespace tlr
{
    namespace avio
    {
        inline const std::vector<std::shared_ptr<IPlugin> >& System::getPlugins() const
        {
            return _plugins;
        }

        template<typename T>
        inline std::shared_ptr<T> System::getPlugin() const
        {
            for (const auto& i : _plugins)
            {
                if (auto plugin = std::dynamic_pointer_cast<T>(i))
                {
                    return plugin;
                }
            }
            return nullptr;
        }
    }
}
