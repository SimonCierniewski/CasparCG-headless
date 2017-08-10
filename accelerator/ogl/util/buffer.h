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

#include <boost/property_tree/ptree_fwd.hpp>

#include <cstdint>

namespace caspar { namespace accelerator { namespace ogl {
			
class buffer final
{
	buffer(const buffer&);
	buffer& operator=(const buffer&);
public:

	// Static Members

	enum class usage
	{
		write_only,
		read_only
	};
	
	// Constructors

	buffer(std::size_t size, usage usage);
	buffer(buffer&& other);
	~buffer();

	// Methods

	buffer& operator=(buffer&& other);
	
	void map();
	void unmap();

	void bind() const;
	void unbind() const;

	// Properties

	uint8_t* data();
	std::size_t size() const;	

	int id() const;

	static boost::property_tree::wptree info();
private:
	struct impl;
	spl::unique_ptr<impl> impl_;
};

}}}