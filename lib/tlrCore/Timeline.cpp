// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021 Darby Johnston
// All rights reserved.

#include <tlrCore/Timeline.h>

#include <tlrCore/AVIOSystem.h>
#include <tlrCore/Assert.h>
#include <tlrCore/Error.h>
#include <tlrCore/File.h>
#include <tlrCore/String.h>
#include <tlrCore/StringFormat.h>

#include <opentimelineio/clip.h>
#include <opentimelineio/externalReference.h>
#include <opentimelineio/imageSequenceReference.h>
#include <opentimelineio/linearTimeWarp.h>
#include <opentimelineio/stackAlgorithm.h>
#include <opentimelineio/timeline.h>
#include <opentimelineio/track.h>
#include <opentimelineio/transition.h>

#if defined(TLR_ENABLE_PYTHON)
#include <Python.h>
#endif

#include <atomic>
#include <array>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace tlr
{
    namespace timeline
    {
        std::vector<std::string> getExtensions()
        {
            //! \todo Get extensions for the Python adapters.
            return { ".otio" };
        }

        std::vector<otime::TimeRange> toRanges(std::vector<otime::RationalTime> frames)
        {
            std::vector<otime::TimeRange> out;
            if (!frames.empty())
            {
                std::sort(frames.begin(), frames.end());
                auto i = frames.begin();
                auto j = i;
                do
                {
                    auto k = j + 1;
                    if (k != frames.end() && (*k - *j).value() > 1)
                    {
                        out.push_back(otime::TimeRange::range_from_start_end_time_inclusive(*i, *j));
                        i = k;
                        j = k;
                    }
                    else if (k == frames.end())
                    {
                        out.push_back(otime::TimeRange::range_from_start_end_time_inclusive(*i, *j));
                        i = k;
                        j = k;
                    }
                    else
                    {
                        ++j;
                    }
                } while (j != frames.end());
            }
            return out;
        }

        const otio::Composable* getRoot(const otio::Composable* composable)
        {
            const otio::Composable* out = composable;
            for (; out->parent(); out = out->parent())
                ;
            return out;
        }

        TLR_ENUM_IMPL(
            Transition,
            "None",
            "Dissolve");
        TLR_ENUM_SERIALIZE_IMPL(Transition);

        Transition toTransition(const std::string& value)
        {
            Transition out = Transition::None;
            if (otio::Transition::Type::SMPTE_Dissolve == value)
            {
                out = Transition::Dissolve;
            }
            return out;
        }

        bool FrameLayer::operator == (const FrameLayer& other) const
        {
            return image == other.image &&
                imageB == other.imageB &&
                transition == other.transition &&
                transitionValue == other.transitionValue;
        }

        bool FrameLayer::operator != (const FrameLayer& other) const
        {
            return !(*this == other);
        }

        bool Frame::operator == (const Frame& other) const
        {
            return time == other.time &&
                layers == other.layers;
        }

        bool Frame::operator != (const Frame& other) const
        {
            return !(*this == other);
        }

        namespace
        {
#if defined(TLR_ENABLE_PYTHON)
            class PyObjectRef
            {
            public:
                PyObjectRef(PyObject* o) :
                    o(o)
                {
                    if (!o)
                    {
                        throw std::runtime_error("Python error");
                    }
                }

                ~PyObjectRef()
                {
                    Py_XDECREF(o);
                }

                PyObject* o = nullptr;

                operator PyObject* () const { return o; }
            };
#endif

            otio::SerializableObject::Retainer<otio::Timeline> read(
                const std::string& fileName,
                otio::ErrorStatus* errorStatus)
            {
                otio::SerializableObject::Retainer<otio::Timeline> out;
#if defined(TLR_ENABLE_PYTHON)
                Py_Initialize();
                try
                {
                    auto pyModule = PyObjectRef(PyImport_ImportModule("opentimelineio.adapters"));

                    auto pyReadFromFile = PyObjectRef(PyObject_GetAttrString(pyModule, "read_from_file"));
                    auto pyReadFromFileArgs = PyObjectRef(PyTuple_New(1));
                    auto pyReadFromFileArg = PyUnicode_FromStringAndSize(fileName.c_str(), fileName.size());
                    if (!pyReadFromFileArg)
                    {
                        throw std::runtime_error("Cannot create arg");
                    }
                    PyTuple_SetItem(pyReadFromFileArgs, 0, pyReadFromFileArg);
                    auto pyTimeline = PyObjectRef(PyObject_CallObject(pyReadFromFile, pyReadFromFileArgs));

                    auto pyToJSONString = PyObjectRef(PyObject_GetAttrString(pyTimeline, "to_json_string"));
                    auto pyJSONString = PyObjectRef(PyObject_CallObject(pyToJSONString, NULL));
                    out = otio::SerializableObject::Retainer<otio::Timeline>(
                        dynamic_cast<otio::Timeline*>(otio::Timeline::from_json_string(
                            PyUnicode_AsUTF8AndSize(pyJSONString, NULL),
                            errorStatus)));
                }
                catch (const std::exception& e)
                {
                    errorStatus->outcome = otio::ErrorStatus::Outcome::FILE_OPEN_FAILED;
                    errorStatus->details = e.what();
                }
                if (PyErr_Occurred())
                {
                    PyErr_Print();
                }
                Py_Finalize();
#else
                out = dynamic_cast<otio::Timeline*>(otio::Timeline::from_json_file(fileName, errorStatus));
#endif
                return out;
            }
        }

        struct Timeline::Private
        {
            file::Path fixPath(const file::Path&) const;
            file::Path getPath(const otio::ImageSequenceReference*) const;
            file::Path getPath(const otio::MediaReference*) const;

            bool getImageInfo(const otio::Composable*, imaging::Info&) const;

            void tick();
            void frameRequests();
            std::future<avio::VideoFrame> readVideoFrame(
                const otio::Track*,
                const otio::Clip*,
                const otime::RationalTime&);
            void stopReaders();
            void delReaders();

            std::shared_ptr<core::Context> context;
            file::Path path;
            otio::SerializableObject::Retainer<otio::Timeline> timeline;
            otime::RationalTime duration = time::invalidTime;
            otime::RationalTime globalStartTime = time::invalidTime;
            imaging::Info imageInfo;
            std::vector<otime::TimeRange> activeRanges;

            struct Request
            {
                Request() {};
                Request(Request&&) = default;

                otime::RationalTime time = time::invalidTime;
                std::promise<Frame> promise;
            };
            std::list<Request> requests;
            std::condition_variable requestCV;
            std::mutex requestMutex;

            struct Reader
            {
                std::shared_ptr<avio::IRead> read;
                avio::Info info;
            };
            std::map<const otio::Clip*, Reader> readers;
            std::list<std::shared_ptr<avio::IRead> > stoppedReaders;

            std::thread thread;
            std::atomic<bool> running;
        };

        void Timeline::_init(
            const file::Path& path,
            const std::shared_ptr<core::Context>& context)
        {
            TLR_PRIVATE_P();

            p.context = context;
            p.path = path;

            // Read the timeline.
            otio::ErrorStatus errorStatus;
            p.timeline = read(p.path.get(), &errorStatus);
            if (errorStatus != otio::ErrorStatus::OK)
            {
                throw std::runtime_error(errorStatus.full_description);
            }
            p.duration = p.timeline.value->duration(&errorStatus);
            if (errorStatus != otio::ErrorStatus::OK)
            {
                throw std::runtime_error(errorStatus.full_description);
            }
            if (p.timeline.value->global_start_time().has_value())
            {
                p.globalStartTime = p.timeline.value->global_start_time().value();
            }
            else
            {
                p.globalStartTime = otime::RationalTime(0, p.duration.rate());
            }

            // Get information about the timeline.
            p.getImageInfo(p.timeline.value->tracks(), p.imageInfo);

            // Create a new thread.
            p.running = true;
            p.thread = std::thread(
                [this]
                {
                    while (_p->running)
                    {
                        _p->tick();
                    }
                });
        }

        Timeline::Timeline() :
            _p(new Private)
        {}

        Timeline::~Timeline()
        {
            TLR_PRIVATE_P();
            p.running = false;
            if (p.thread.joinable())
            {
                p.thread.join();
            }
        }

        std::shared_ptr<Timeline> Timeline::create(
            const file::Path& path,
            const std::shared_ptr<core::Context>& context)
        {
            auto out = std::shared_ptr<Timeline>(new Timeline);
            out->_init(path, context);
            return out;
        }

        const std::shared_ptr<core::Context>& Timeline::getContext() const
        {
            return _p->context;
        }

        const file::Path& Timeline::getPath() const
        {
            return _p->path;
        }

        const otime::RationalTime& Timeline::getGlobalStartTime() const
        {
            return _p->globalStartTime;
        }

        const otime::RationalTime& Timeline::getDuration() const
        {
            return _p->duration;
        }

        const imaging::Info& Timeline::getImageInfo() const
        {
            return _p->imageInfo;
        }

        std::future<Frame> Timeline::getFrame(const otime::RationalTime& time)
        {
            TLR_PRIVATE_P();
            Private::Request request;
            request.time = time;
            auto future = request.promise.get_future();
            {
                std::unique_lock<std::mutex> lock(p.requestMutex);
                p.requests.push_back(std::move(request));
            }
            p.requestCV.notify_one();
            return future;
        }

        void Timeline::setActiveRanges(const std::vector<otime::TimeRange>& ranges)
        {
            _p->activeRanges = ranges;
        }

        void Timeline::cancelFrames()
        {
            TLR_PRIVATE_P();
            std::unique_lock<std::mutex> lock(p.requestMutex);
            p.requests.clear();
            for (auto& i : p.readers)
            {
                i.second.read->cancelVideoFrames();
            }
        }

        file::Path Timeline::Private::fixPath(const file::Path& path) const
        {
            std::string directory;
            if (!path.isAbsolute())
            {
                directory = this->path.getDirectory();
            }
            return file::Path(directory, path.get());
        }

        file::Path Timeline::Private::getPath(const otio::ImageSequenceReference* ref) const
        {
            std::stringstream ss;
            ss << ref->target_url_base() <<
                ref->name_prefix() <<
                std::setfill('0') << std::setw(ref->frame_zero_padding()) << ref->start_frame() <<
                ref->name_suffix();
            return file::Path(ss.str());
        }

        file::Path Timeline::Private::getPath(const otio::MediaReference* ref) const
        {
            file::Path out;
            if (auto externalRef = dynamic_cast<const otio::ExternalReference*>(ref))
            {
                //! \bug Handle URL parsing.
                out = file::Path(externalRef->target_url());
            }
            else if (auto imageSequenceRef = dynamic_cast<const otio::ImageSequenceReference*>(ref))
            {
                out = getPath(imageSequenceRef);
            }
            return fixPath(out);
        }

        bool Timeline::Private::getImageInfo(const otio::Composable* composable, imaging::Info& imageInfo) const
        {
            if (auto clip = dynamic_cast<const otio::Clip*>(composable))
            {
                // The first clip with video defines the image information
                // for the timeline.
                avio::Options options;
                otio::ErrorStatus errorStatus;
                options["SequenceIO/DefaultSpeed"] = string::Format("{0}").arg(clip->duration(&errorStatus).rate());
                if (auto read = context->getSystem<avio::System>()->read(getPath(clip->media_reference()), options))
                {
                    const auto info = read->getInfo().get();
                    if (!info.video.empty())
                    {
                        imageInfo = info.video[0];
                        return true;
                    }
                }
            }
            if (auto composition = dynamic_cast<const otio::Composition*>(composable))
            {
                for (const auto& child : composition->children())
                {
                    if (getImageInfo(child, imageInfo))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        void Timeline::Private::tick()
        {
            frameRequests();
            stopReaders();
            delReaders();
        }

        void Timeline::Private::frameRequests()
        {
            struct LayerData
            {
                LayerData() {};
                LayerData(LayerData&&) = default;

                std::future<avio::VideoFrame> image;
                std::future<avio::VideoFrame> imageB;
                Transition transition = Transition::None;
                float transitionValue = 0.F;
            };
            struct Result
            {
                otime::RationalTime time = time::invalidTime;
                std::vector<LayerData> layerData;
                std::promise<Frame> promise;
            };
            Result result;
            bool resultValid = false;
            {
                std::unique_lock<std::mutex> lock(requestMutex);
                requestCV.wait_for(
                    lock,
                    requestTimeout,
                    [this]
                    {
                        return !requests.empty();
                    });
                if (!requests.empty())
                {
                    result.time = requests.front().time;
                    result.promise = std::move(requests.front().promise);
                    resultValid = true;
                    requests.pop_front();
                }
            }
            if (resultValid)
            {
                Frame frame;
                frame.time = result.time;
                try
                {
                    for (const auto& j : timeline->tracks()->children())
                    {
                        const auto track = dynamic_cast<otio::Track*>(j.value);
                        if (track && otio::Track::Kind::video == track->kind())
                        {
                            for (const auto& k : track->children())
                            {
                                if (const auto clip = dynamic_cast<otio::Clip*>(k.value))
                                {
                                    otio::ErrorStatus errorStatus;
                                    const auto rangeOpt = clip->trimmed_range_in_parent(&errorStatus);
                                    if (rangeOpt.has_value())
                                    {
                                        const auto range = rangeOpt.value();
                                        const auto time = result.time - globalStartTime;
                                        if (range.contains(time))
                                        {
                                            LayerData data;
                                            data.image = readVideoFrame(track, clip, time);
                                            auto clipStartTime = clip->trimmed_range(&errorStatus).start_time();
                                            const auto neighbors = track->neighbors_of(clip, &errorStatus);
                                            if (auto transition = dynamic_cast<otio::Transition*>(neighbors.second.value))
                                            {
                                                const auto transitionStartTime = range.end_time_inclusive() - transition->in_offset();
                                                if (time > transitionStartTime)
                                                {
                                                    const auto transitionNeighbors = track->neighbors_of(transition, &errorStatus);
                                                    if (const auto clipB = dynamic_cast<otio::Clip*>(transitionNeighbors.second.value))
                                                    {
                                                        data.imageB = readVideoFrame(track, clipB, time);
                                                        data.transition = toTransition(transition->transition_type());
                                                        data.transitionValue = otime::RationalTime(time - transitionStartTime).value() /
                                                            (transition->in_offset().value() + transition->out_offset().value() + 1.0);
                                                    }
                                                }
                                            }
                                            if (auto transition = dynamic_cast<otio::Transition*>(neighbors.first.value))
                                            {
                                                clipStartTime -= transition->in_offset();
                                                const auto transitionEndTime = range.start_time() + transition->out_offset();
                                                if (time < transitionEndTime)
                                                {
                                                    const auto transitionNeighbors = track->neighbors_of(transition, &errorStatus);
                                                    if (const auto clipB = dynamic_cast<otio::Clip*>(transitionNeighbors.first.value))
                                                    {
                                                        data.imageB = readVideoFrame(track, clipB, time);
                                                        data.transition = toTransition(transition->transition_type());
                                                        data.transitionValue = 1.F - (otime::RationalTime(time - range.start_time() + transition->in_offset()).value() + 1.0) /
                                                            (transition->in_offset().value() + transition->out_offset().value() + 1.0);
                                                    }
                                                }
                                            }
                                            result.layerData.push_back(std::move(data));
                                        }
                                    }
                                }
                            }
                        }
                    }
                    for (auto& j : result.layerData)
                    {
                        FrameLayer layer;
                        layer.image = j.image.get().image;
                        if (j.imageB.valid())
                        {
                            layer.imageB = j.imageB.get().image;
                        }
                        layer.transition = j.transition;
                        layer.transitionValue = j.transitionValue;
                        frame.layers.push_back(layer);
                    }
                }
                catch (const std::exception&)
                {
                    //! \todo How should this be handled?
                }
                result.promise.set_value(frame);
            }
        }

        std::future<avio::VideoFrame> Timeline::Private::readVideoFrame(
            const otio::Track* track,
            const otio::Clip* clip,
            const otime::RationalTime& time)
        {
            std::future<avio::VideoFrame> out;

            // Get the clip time transform.
            //
            //! \bug This only applies time transform at the clip level.
            otio::TimeTransform timeTransform;
            for (const auto& effect : clip->effects())
            {
                if (auto linearTimeWarp = dynamic_cast<otio::LinearTimeWarp*>(effect.value))
                {
                    timeTransform = otio::TimeTransform(otime::RationalTime(), linearTimeWarp->time_scalar()).applied_to(timeTransform);
                }
            }

            // Get the clip start time taking transitions into account.
            otime::RationalTime startTime;
            otio::ErrorStatus errorStatus;
            const auto range = clip->trimmed_range(&errorStatus);
            startTime = range.start_time();
            const auto neighbors = track->neighbors_of(clip, &errorStatus);
            if (auto transition = dynamic_cast<const otio::Transition*>(neighbors.first.value))
            {
                startTime -= transition->in_offset();
            }

            // Get the frame time.
            const auto clipTime = track->transformed_time(time, clip, &errorStatus);
            auto frameTime = startTime + timeTransform.applied_to(clipTime - startTime);

            // Read the frame.
            const auto ioSystem = context->getSystem<avio::System>();
            const auto j = readers.find(clip);
            if (j != readers.end())
            {
                const auto readTime = frameTime.rescaled_to(j->second.info.videoDuration);
                const auto floorTime = otime::RationalTime(floor(readTime.value()), readTime.rate());
                out = j->second.read->readVideoFrame(floorTime);
            }
            else
            {
                const file::Path path = getPath(clip->media_reference());
                avio::Options options;
                options["SequenceIO/DefaultSpeed"] = string::Format("{0}").arg(duration.rate());
                auto read = ioSystem->read(path, options);
                avio::Info info;
                if (read)
                {
                    info = read->getInfo().get();
                }
                if (read && !info.video.empty())
                {
                    context->log("tlr::timeline::Timeline", this->path.get() + ": Read: " + path.get());
                    Reader reader;
                    reader.read = read;
                    reader.info = info;
                    const auto readTime = frameTime.rescaled_to(info.videoDuration);
                    const auto floorTime = otime::RationalTime(floor(readTime.value()), readTime.rate());
                    out = read->readVideoFrame(floorTime);
                    readers[clip] = std::move(reader);
                }
            }

            return out;
        }

        void Timeline::Private::stopReaders()
        {
            auto i = readers.begin();
            while (i != readers.end())
            {
                const auto clip = i->first;

                otio::ErrorStatus errorStatus;
                const auto trimmedRange = clip->trimmed_range(&errorStatus);
                const auto ancestor = dynamic_cast<const otio::Item*>(getRoot(clip));
                const auto clipRange = i->first->transformed_time_range(trimmedRange, ancestor, &errorStatus);
                auto startTime = clipRange.start_time();
                auto endTime = startTime + clipRange.duration();
                const auto track = getParent<otio::Track>(clip);
                const auto neighbors = track->neighbors_of(clip, &errorStatus);
                if (auto transition = dynamic_cast<const otio::Transition*>(neighbors.first.value))
                {
                    startTime -= transition->in_offset();
                }
                if (auto transition = dynamic_cast<const otio::Transition*>(neighbors.second.value))
                {
                    endTime += transition->out_offset();
                }
                const auto range = otime::TimeRange::range_from_start_end_time(globalStartTime + startTime, globalStartTime + endTime);

                bool del = true;
                for (const auto& activeRange : activeRanges)
                {
                    if (range.intersects(activeRange))
                    {
                        del = false;
                        break;
                    }
                }
                if (del && !i->second.read->hasVideoFrames())
                {
                    context->log("tlr::timeline::Timeline", path.get() + ": Stop: " + i->second.read->getPath().get());
                    auto read = i->second.read;
                    read->stop();
                    stoppedReaders.push_back(read);
                    i = readers.erase(i);
                }
                else
                {
                    ++i;
                }
            }
        }

        void Timeline::Private::delReaders()
        {
            auto i = stoppedReaders.begin();
            while (i != stoppedReaders.end())
            {
                if ((*i)->hasStopped())
                {
                    context->log("tlr::timeline::Timeline", path.get() + ": Delete: " + (*i)->getPath().get());
                    i = stoppedReaders.erase(i);
                }
                else
                {
                    ++i;
                }
            }
        }
    }
}
