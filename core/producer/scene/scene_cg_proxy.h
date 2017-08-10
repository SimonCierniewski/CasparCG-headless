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
* Author: Helge Norberg, helge.norberg@svt.se
*/

#pragma once

#include <core/producer/cg_proxy.h>
#include <common/memory.h>

namespace caspar { namespace core { namespace scene {

class scene_cg_proxy : public cg_proxy
{
public:
	scene_cg_proxy(spl::shared_ptr<frame_producer> producer);

	void add(int layer, const std::wstring& template_name, bool play_on_load, const std::wstring& start_from_label, const std::wstring& data) override;
	void remove(int layer) override;
	void play(int layer) override;
	void stop(int layer, unsigned int mix_out_duration) override;
	void next(int layer) override;
	void update(int layer, const std::wstring& data) override;
	std::wstring invoke(int layer, const std::wstring& label) override;
	std::wstring description(int layer) override;
	std::wstring template_host_info() override;
private:
	struct impl;
	spl::shared_ptr<impl> impl_;
};

}}}
