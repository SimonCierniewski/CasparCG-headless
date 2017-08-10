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

#include <common/forward.h>
#include <common/memory.h>
#include <common/cache_aligned_vector.h>
#include <common/array.h>

#include <core/frame/frame_visitor.h>
#include <core/monitor/monitor.h>

#include <vector>
#include <cstdint>

FORWARD2(caspar, diagnostics, class graph);

namespace caspar { namespace core {
		
typedef caspar::array<const int32_t> audio_buffer;

class audio_mixer final : public frame_visitor
{
	audio_mixer(const audio_mixer&);
	audio_mixer& operator=(const audio_mixer&);
public:

	// Static Members

	// Constructors

	audio_mixer(spl::shared_ptr<::caspar::diagnostics::graph> graph);

	// Methods
	
	audio_buffer operator()(const struct video_format_desc& format_desc, const struct audio_channel_layout& channel_layout);
	void set_master_volume(float volume); 
	float get_master_volume();
	monitor::subject& monitor_output();

	// frame_visitor

	virtual void push(const struct frame_transform& transform);
	virtual void visit(const class const_frame& frame);
	virtual void pop();
	
	// Properties

private:
	struct impl;
	spl::shared_ptr<impl> impl_;
};

}}