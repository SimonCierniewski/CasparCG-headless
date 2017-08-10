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

#include <common/memory.h>
#include <common/forward.h>

#include "fwd.h"

#include "monitor/monitor.h"

#include <boost/property_tree/ptree_fwd.hpp>

#include <functional>

namespace caspar { namespace core {

class video_channel final
{
	video_channel(const video_channel&);
	video_channel& operator=(const video_channel&);
public:

	// Static Members

	// Constructors

	explicit video_channel(
			int index,
			const video_format_desc& format_desc,
			const audio_channel_layout& channel_layout,
			std::unique_ptr<image_mixer> image_mixer);
	~video_channel();

	// Methods

	monitor::subject&						monitor_output();

	// Properties

	const core::stage&						stage() const;
	core::stage&							stage();
	const core::mixer&						mixer() const;
	core::mixer&							mixer();
	const core::output&						output() const;
	core::output&							output();

	core::video_format_desc					video_format_desc() const;
	void									video_format_desc(const core::video_format_desc& format_desc);
	core::audio_channel_layout				audio_channel_layout() const;
	void									audio_channel_layout(const core::audio_channel_layout& channel_layout);

	std::shared_ptr<void>					add_tick_listener(std::function<void()> listener);

	spl::shared_ptr<core::frame_factory>	frame_factory();

	boost::property_tree::wptree			info() const;
	boost::property_tree::wptree			delay_info() const;
	int										index() const;
private:
	struct impl;
	spl::unique_ptr<impl> impl_;
};

}}
