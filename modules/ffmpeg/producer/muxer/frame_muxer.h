/*
* Copyright (c) 2011 Sveriges Television AB <info@casparcg.com>
*
* This file is part of CasparCG (www.casparcg.com).
*
* CasparCG is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* CasparCG is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with CasparCG. If not, see <http://www.gnu.org/licenses/>.
*
* Author: Robert Nagy, ronag89@gmail.com
*/

#pragma once

#include "display_mode.h"
#include "../filter/audio_filter.h"

#include <common/forward.h>
#include <common/memory.h>
#include <common/env.h>

#include <core/frame/frame.h>
#include <core/mixer/audio/audio_mixer.h>
#include <core/fwd.h>

#include <boost/noncopyable.hpp>
#include <boost/rational.hpp>

#include <vector>

struct AVFrame;

namespace caspar { namespace ffmpeg {

class frame_muxer : boost::noncopyable
{
public:
	frame_muxer(
			boost::rational<int> in_framerate,
			std::vector<audio_input_pad> audio_input_pads,
			const spl::shared_ptr<core::frame_factory>& frame_factory,
			const core::video_format_desc& format_desc,
			const core::audio_channel_layout& channel_layout,
			const std::wstring& filter,
			bool multithreaded_filter,
			bool force_deinterlacing = env::properties().get(L"configuration.force-deinterlace", false));

	void push(const std::shared_ptr<AVFrame>& video_frame);
	void push(const std::vector<std::shared_ptr<core::mutable_audio_buffer>>& audio_samples_per_stream);

	bool video_ready() const;
	bool audio_ready() const;

	core::draw_frame poll();

	boost::rational<int> out_framerate() const;

	uint32_t calc_nb_frames(uint32_t nb_frames) const;
private:
	struct impl;
	spl::shared_ptr<impl> impl_;
};

}}
