// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021 Darby Johnston
// All rights reserved.

#include <tlrQt/TimelineWidget.h>

#include <tlrQt/TimelineControls.h>
#include <tlrQt/TimelineSlider.h>
#include <tlrQt/TimelineViewport.h>

#include <QVBoxLayout>

namespace tlr
{
    namespace qt
    {
        struct TimelineWidget::Private
        {
            TimelineViewport* viewport = nullptr;
            TimelineSlider* slider = nullptr;
            TimelineControls* controls = nullptr;
        };

        TimelineWidget::TimelineWidget(QWidget* parent) :
            QWidget(parent),
            _p(new Private)
        {
            TLR_PRIVATE_P();

            p.viewport = new TimelineViewport;

            p.slider = new TimelineSlider;
            p.slider->setToolTip(tr("Timeline slider"));

            p.controls = new TimelineControls;

            auto layout = new QVBoxLayout;
            layout->setMargin(0);
            layout->setSpacing(0);
            layout->addWidget(p.viewport, 1);
            auto vLayout = new QVBoxLayout;
            vLayout->setMargin(5);
            vLayout->setSpacing(5);
            vLayout->addWidget(p.slider, 1);
            vLayout->addWidget(p.controls);
            layout->addLayout(vLayout);
            setLayout(layout);
        }

        void TimelineWidget::setTimeObject(TimeObject* timeObject)
        {
            TLR_PRIVATE_P();
            p.slider->setTimeObject(timeObject);
            p.controls->setTimeObject(timeObject);
        }

        void TimelineWidget::setColorConfig(const gl::ColorConfig& colorConfig)
        {
            TLR_PRIVATE_P();
            p.viewport->setColorConfig(colorConfig);
            p.slider->setColorConfig(colorConfig);
        }

        void TimelineWidget::setTimelinePlayer(TimelinePlayer* timelinePlayer)
        {
            TLR_PRIVATE_P();
            p.viewport->setTimelinePlayer(timelinePlayer);
            p.slider->setTimelinePlayer(timelinePlayer);
            p.controls->setTimelinePlayer(timelinePlayer);
        }
    }
}
