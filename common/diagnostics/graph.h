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

#include "../memory.h"

#include <string>
#include <tuple>
#include <functional>

#include <boost/noncopyable.hpp>

namespace caspar { namespace diagnostics {
	
int color(float r, float g, float b, float a = 1.0f);
std::tuple<float, float, float, float> color(int code);

enum class tag_severity
{
	WARNING,
	INFO
};

class graph : boost::noncopyable
{
	friend void register_graph(const spl::shared_ptr<graph>& graph);
public:
	graph();
	void set_text(const std::wstring& value);
	void set_value(const std::string& name, double value);
	void set_color(const std::string& name, int color);
	void set_tag(tag_severity severity, const std::string& name);
	void auto_reset();
private:
	struct impl;
	std::shared_ptr<impl> impl_;
};

void register_graph(const spl::shared_ptr<graph>& graph);

namespace spi {

class graph_sink : boost::noncopyable
{
public:
	virtual ~graph_sink() { }
	virtual void activate() = 0;
	virtual void set_text(const std::wstring& value) = 0;
	virtual void set_value(const std::string& name, double value) = 0;
	virtual void set_color(const std::string& name, int color) = 0;
	virtual void set_tag(tag_severity severity, const std::string& name) = 0;
	virtual void auto_reset() = 0;
};

typedef std::function<spl::shared_ptr<graph_sink>()> sink_factory_t;
void register_sink_factory(sink_factory_t factory);

}

}}