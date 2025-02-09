// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021 Darby Johnston
// All rights reserved.

#include <tlrQt/TimeSpinBox.h>

#include <QApplication>
#include <QFontDatabase>
#include <QLineEdit>
#include <QRegExpValidator>
#include <QStyleOptionSpinBox>

namespace tlr
{
    namespace qt
    {
        struct TimeSpinBox::Private
        {
            otime::RationalTime value = time::invalidTime;
            TimeUnits units = TimeUnits::Timecode;
            QRegExpValidator* validator = nullptr;
            TimeObject* timeObject = nullptr;
        };

        TimeSpinBox::TimeSpinBox(QWidget* parent) :
            QAbstractSpinBox(parent),
            _p(new Private)
        {
            const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            setFont(fixedFont);

            _vaidatorUpdate();
            _textUpdate();

            connect(
                lineEdit(),
                SIGNAL(returnPressed()),
                SLOT(_lineEditCallback()));
            connect(
                lineEdit(),
                SIGNAL(editingFinished()),
                SLOT(_lineEditCallback()));
        }

        void TimeSpinBox::setTimeObject(TimeObject* timeObject)
        {
            TLR_PRIVATE_P();
            if (timeObject == p.timeObject)
                return;
            if (p.timeObject)
            {
                disconnect(
                    p.timeObject,
                    SIGNAL(unitsChanged(qt::TimeUnits)),
                    this,
                    SLOT(setUnits(qt::TimeUnits)));
            }
            p.timeObject = timeObject;
            if (p.timeObject)
            {
                p.units = p.timeObject->units();
                connect(
                    p.timeObject,
                    SIGNAL(unitsChanged(qt::TimeUnits)),
                    SLOT(setUnits(qt::TimeUnits)));
            }
            _vaidatorUpdate();
            _textUpdate();
            updateGeometry();
        }

        const otime::RationalTime& TimeSpinBox::value() const
        {
            return _p->value;
        }

        TimeUnits TimeSpinBox::units() const
        {
            return _p->units;
        }

        void TimeSpinBox::stepBy(int steps)
        {
            TLR_PRIVATE_P();
            p.value += otime::RationalTime(steps, p.value.rate());
            Q_EMIT valueChanged(p.value);
            _textUpdate();
        }

        QValidator::State TimeSpinBox::validate(QString&, int& pos) const
        {
            return QValidator::Acceptable;
        }

        void TimeSpinBox::setValue(const otime::RationalTime& value)
        {
            TLR_PRIVATE_P();
            if (p.value == value)
                return;
            p.value = value;
            Q_EMIT valueChanged(p.value);
            _textUpdate();
        }

        void TimeSpinBox::setUnits(TimeUnits units)
        {
            TLR_PRIVATE_P();
            if (p.units == units)
                return;
            p.units = units;
            Q_EMIT unitsChanged(p.units);
            _vaidatorUpdate();
            _textUpdate();
            updateGeometry();
        }

        QAbstractSpinBox::StepEnabled TimeSpinBox::stepEnabled() const
        {
            return QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
        }

        QSize TimeSpinBox::minimumSizeHint() const
        {
            TLR_PRIVATE_P();
            //! \todo Cache the size hint.
            ensurePolished();
            int h = lineEdit()->minimumSizeHint().height();
            const QFontMetrics fm(fontMetrics());
            int w = fm.horizontalAdvance(" " + sizeHintString(p.units));
            w += 2; // cursor blinking space
            QStyleOptionSpinBox opt;
            initStyleOption(&opt);
            QSize hint(w, h);
            return style()->sizeFromContents(QStyle::CT_SpinBox, &opt, hint, this)
                .expandedTo(QApplication::globalStrut());
        }

        void TimeSpinBox::_lineEditCallback()
        {
            TLR_PRIVATE_P();
            otime::ErrorStatus errorStatus;
            const auto time = textToTime(lineEdit()->text(), p.value.rate(), p.units, &errorStatus);
            if (otime::ErrorStatus::OK == errorStatus && time != p.value)
            {
                p.value = time;
                Q_EMIT valueChanged(p.value);
            }
            _textUpdate();
        }

        void TimeSpinBox::_vaidatorUpdate()
        {
            TLR_PRIVATE_P();
            if (p.validator)
            {
                p.validator->setParent(nullptr);
            }
            p.validator = new QRegExpValidator(QRegExp(validator(p.units)), this);
            lineEdit()->setValidator(p.validator);
        }

        void TimeSpinBox::_textUpdate()
        {
            TLR_PRIVATE_P();
            lineEdit()->setText(timeToText(p.value, p.units));
        }
    }
}
