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

#include <core/video_format.h>
#include <core/consumer/frame_consumer.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/filesystem.hpp>

#include <string>
#include <vector>

namespace caspar {

namespace image {

void write_cropped_png(
		const class core::const_frame& frame,
		const core::video_format_desc& format_desc,
		const boost::filesystem::path& output_file,
		int width,
		int height);

void describe_consumer(core::help_sink& sink, const core::help_repository& repo);
spl::shared_ptr<core::frame_consumer> create_consumer(
		const std::vector<std::wstring>& params, struct core::interaction_sink*, std::vector<spl::shared_ptr<core::video_channel>> channels);

}}
