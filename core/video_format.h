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

#include <vector>
#include <string>
#include <cstddef>

#include <common/enum_class.h>

#include <boost/rational.hpp>

namespace caspar { namespace core {

enum class video_format
{
	pal,
	ntsc,
	x576p2500,
	x720p2500,
	x720p5000,
	x720p2398,
	x720p2400,
	x720p2997,
	x720p5994,
	x720p3000,
	x720p6000,
	x1080p2398,
	x1080p2400,
	x1080i5000,
	x1080i5994,
	x1080i6000,
	x1080p2500,
	x1080p2997,
	x1080p3000,
	x1080p5000,
	x1080p5994,
	x1080p6000,
	x1556p2398,
	x1556p2400,
	x1556p2500,
	dci1080p2398,
	dci1080p2400,
	dci1080p2500,
	x2160p2398,
	x2160p2400,
	x2160p2500,
	x2160p2997,
	x2160p3000,
	x2160p5000,
	x2160p5994,
	x2160p6000,
	dci2160p2398,
	dci2160p2400,
	dci2160p2500,
	invalid,
	count
};

enum class field_mode
{
	empty		= 0,
	lower		= 1,
	upper		= 2,
	progressive = 3 // NOTE: progressive == lower | upper;
};
ENUM_ENABLE_BITWISE(field_mode);
//static_assert((field_mode::lower | field_mode::upper) == field_mode::progressive, "");

struct video_format_desc final
{
	video_format			format;

	int						width;
	int						height;
	int						square_width;
	int						square_height;
	core::field_mode		field_mode;	// progressive, interlaced upper field first, interlaced lower field first
	double					fps;		// actual framerate = duration/time_scale, e.g. i50 = 25 fps, p50 = 50 fps
	boost::rational<int>	framerate;
	int						time_scale;
	int						duration;
	int						field_count;
	std::size_t				size;		// frame size in bytes
	std::wstring			name;		// name of output format

	int						audio_sample_rate;
	std::vector<int>		audio_cadence;	// rotating optimal number of samples per frame

	video_format_desc(video_format format,
					  int width,
					  int height,
					  int square_width,
					  int square_height,
					  core::field_mode field_mode,
					  int time_scale,
					  int duration,
					  const std::wstring& name,
					  const std::vector<int>& audio_cadence);

	video_format_desc(video_format format = video_format::invalid);
	video_format_desc(const std::wstring& name);
};

bool operator==(const video_format_desc& rhs, const video_format_desc& lhs);
bool operator!=(const video_format_desc& rhs, const video_format_desc& lhs);

std::wostream& operator<<(std::wostream& out, const video_format_desc& format_desc);

std::vector<int> find_audio_cadence(const boost::rational<int>& framerate, bool log_quiet = false);

}}
