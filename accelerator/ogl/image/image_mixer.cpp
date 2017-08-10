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
#include "../../StdAfx.h"

#include "image_mixer.h"

#include "image_kernel.h"

#include "../util/device.h"
#include "../util/buffer.h"
#include "../util/texture.h"

#include <common/gl/gl_check.h>
#include <common/future.h>
#include <common/array.h>
#include <common/linq.h>

#include <core/frame/frame.h>
#include <core/frame/frame_transform.h>
#include <core/frame/pixel_format.h>
#include <core/video_format.h>
#include <core/frame/geometry.h>

#include <GL/glew.h>

#include <boost/range/algorithm_ext/erase.hpp>
#include <boost/thread/future.hpp>

#include <algorithm>
#include <vector>

namespace caspar { namespace accelerator { namespace ogl {

typedef std::shared_future<std::shared_ptr<texture>> future_texture;

struct item
{
	core::pixel_format_desc		pix_desc	= core::pixel_format::invalid;
	std::vector<future_texture>	textures;
	core::image_transform		transform;
	core::frame_geometry		geometry	= core::frame_geometry::get_default();
};

struct layer
{
	std::vector<layer>	sublayers;
	std::vector<item>	items;
	core::blend_mode	blend_mode;

	layer(core::blend_mode blend_mode)
		: blend_mode(blend_mode)
	{
	}
};

std::size_t get_max_video_format_size()
{
	auto format_size = [](core::video_format format) { return core::video_format_desc(format).size; };
	return cpplinq::from(enum_constants<core::video_format>())
		.select(std::ref(format_size))
		.max();
}

class image_renderer
{
	spl::shared_ptr<device>	ogl_;
	image_kernel			kernel_;
public:
	image_renderer(const spl::shared_ptr<device>& ogl, bool blend_modes_wanted, bool straight_alpha_wanted)
		: ogl_(ogl)
		, kernel_(ogl_, blend_modes_wanted, straight_alpha_wanted)
	{
	}

	std::future<array<const std::uint8_t>> operator()(std::vector<layer> layers, const core::video_format_desc& format_desc, bool straighten_alpha)
	{
		if(layers.empty())
		{ // Bypass GPU with empty frame.
			static const cache_aligned_vector<uint8_t> buffer(get_max_video_format_size(), 0);
			return make_ready_future(array<const std::uint8_t>(buffer.data(), format_desc.size, true));
		}

		return flatten(ogl_->begin_invoke([=]() mutable -> std::shared_future<array<const std::uint8_t>>
		{
			auto target_texture = ogl_->create_texture(format_desc.width, format_desc.height, 4, false);

			if (format_desc.field_mode != core::field_mode::progressive)
			{
				draw(target_texture, layers, format_desc, core::field_mode::upper);
				draw(target_texture, std::move(layers), format_desc, core::field_mode::lower);
			}
			else
				draw(target_texture, std::move(layers), format_desc, core::field_mode::progressive);

			kernel_.post_process(target_texture, straighten_alpha);

			target_texture->attach();

			return ogl_->copy_async(target_texture);
		}));
	}

private:

	void draw(spl::shared_ptr<texture>&			target_texture,
			  std::vector<layer>				layers,
			  const core::video_format_desc&	format_desc,
			  core::field_mode					field_mode)
	{
		std::shared_ptr<texture> layer_key_texture;

		for (auto& layer : layers)
		{
			draw(target_texture, layer.sublayers, format_desc, field_mode);
			draw(target_texture, std::move(layer), layer_key_texture, format_desc, field_mode);
		}
	}

	void draw(spl::shared_ptr<texture>&			target_texture,
			  layer								layer,
			  std::shared_ptr<texture>&			layer_key_texture,
			  const core::video_format_desc&	format_desc,
			  core::field_mode					field_mode)
	{
		// REMOVED: This is done in frame_muxer.
		// Fix frames
		//BOOST_FOREACH(auto& item, layer.items)
		//{
			//if(std::abs(item.transform.fill_scale[1]-1.0) > 1.0/target_texture->height() ||
			//   std::abs(item.transform.fill_translation[1]) > 1.0/target_texture->height())
			//	CASPAR_LOG(warning) << L"[image_mixer] Frame should be deinterlaced. Send FILTER DEINTERLACE_BOB when creating producer.";

			//if(item.pix_desc.planes.at(0).height == 480) // NTSC DV
			//{
			//	item.transform.fill_translation[1] += 2.0/static_cast<double>(format_desc.height);
			//	item.transform.fill_scale[1] *= 1.0 - 6.0*1.0/static_cast<double>(format_desc.height);
			//}

			//// Fix field-order if needed
			//if(item.field_mode == core::field_mode::lower && format_desc.field_mode == core::field_mode::upper)
			//	item.transform.fill_translation[1] += 1.0/static_cast<double>(format_desc.height);
			//else if(item.field_mode == core::field_mode::upper && format_desc.field_mode == core::field_mode::lower)
			//	item.transform.fill_translation[1] -= 1.0/static_cast<double>(format_desc.height);
		//}

		// Mask out fields
		for (auto& item : layer.items)
			item.transform.field_mode &= field_mode;

		// Remove empty items.
		boost::range::remove_erase_if(layer.items, [&](const item& item)
		{
			return item.transform.field_mode == core::field_mode::empty;
		});

		if(layer.items.empty())
			return;

		std::shared_ptr<texture> local_key_texture;
		std::shared_ptr<texture> local_mix_texture;

		if(layer.blend_mode != core::blend_mode::normal)
		{
			auto layer_texture = ogl_->create_texture(target_texture->width(), target_texture->height(), 4, false);

			for (auto& item : layer.items)
				draw(layer_texture, std::move(item), layer_key_texture, local_key_texture, local_mix_texture, format_desc);

			draw(layer_texture, std::move(local_mix_texture), core::blend_mode::normal);
			draw(target_texture, std::move(layer_texture), layer.blend_mode);
		}
		else // fast path
		{
			for (auto& item : layer.items)
				draw(target_texture, std::move(item), layer_key_texture, local_key_texture, local_mix_texture, format_desc);

			draw(target_texture, std::move(local_mix_texture), core::blend_mode::normal);
		}

		layer_key_texture = std::move(local_key_texture);
	}

	void draw(spl::shared_ptr<texture>& target_texture,
			  item item,
		      std::shared_ptr<texture>& layer_key_texture,
			  std::shared_ptr<texture>& local_key_texture,
			  std::shared_ptr<texture>& local_mix_texture,
			  const core::video_format_desc& format_desc)
	{
		draw_params draw_params;
		draw_params.pix_desc		= std::move(item.pix_desc);
		draw_params.transform		= std::move(item.transform);
		draw_params.geometry		= item.geometry;
		draw_params.aspect_ratio	= static_cast<double>(format_desc.square_width) / static_cast<double>(format_desc.square_height);

		for (auto& future_texture : item.textures)
			draw_params.textures.push_back(spl::make_shared_ptr(future_texture.get()));

		if(item.transform.is_key)
		{
			local_key_texture = local_key_texture ? local_key_texture : ogl_->create_texture(target_texture->width(), target_texture->height(), 1, draw_params.transform.use_mipmap);

			draw_params.background	= local_key_texture;
			draw_params.local_key	= nullptr;
			draw_params.layer_key	= nullptr;

			kernel_.draw(std::move(draw_params));
		}
		else if(item.transform.is_mix)
		{
			local_mix_texture = local_mix_texture ? local_mix_texture : ogl_->create_texture(target_texture->width(), target_texture->height(), 4, draw_params.transform.use_mipmap);

			draw_params.background	= local_mix_texture;
			draw_params.local_key	= std::move(local_key_texture);
			draw_params.layer_key	= layer_key_texture;

			draw_params.keyer		= keyer::additive;

			kernel_.draw(std::move(draw_params));
		}
		else
		{
			draw(target_texture, std::move(local_mix_texture), core::blend_mode::normal);

			draw_params.background	= target_texture;
			draw_params.local_key	= std::move(local_key_texture);
			draw_params.layer_key	= layer_key_texture;

			kernel_.draw(std::move(draw_params));
		}
	}

	void draw(spl::shared_ptr<texture>&	 target_texture,
			  std::shared_ptr<texture>&& source_buffer,
			  core::blend_mode			 blend_mode = core::blend_mode::normal)
	{
		if(!source_buffer)
			return;

		draw_params draw_params;
		draw_params.pix_desc.format	= core::pixel_format::bgra;
		draw_params.pix_desc.planes	= { core::pixel_format_desc::plane(source_buffer->width(), source_buffer->height(), 4) };
		draw_params.textures		= { spl::make_shared_ptr(source_buffer) };
		draw_params.transform		= core::image_transform();
		draw_params.blend_mode		= blend_mode;
		draw_params.background		= target_texture;
		draw_params.geometry		= core::frame_geometry::get_default();

		kernel_.draw(std::move(draw_params));
	}
};

struct image_mixer::impl : public core::frame_factory
{
	spl::shared_ptr<device>				ogl_;
	image_renderer						renderer_;
	std::vector<core::image_transform>	transform_stack_;
	std::vector<layer>					layers_; // layer/stream/items
	std::vector<layer*>					layer_stack_;
public:
	impl(const spl::shared_ptr<device>& ogl, bool blend_modes_wanted, bool straight_alpha_wanted, int channel_id)
		: ogl_(ogl)
		, renderer_(ogl, blend_modes_wanted, straight_alpha_wanted)
		, transform_stack_(1)
	{
		CASPAR_LOG(info) << L"Initialized OpenGL Accelerated GPU Image Mixer for channel " << channel_id;
	}

	void push(const core::frame_transform& transform)
	{
		auto previous_layer_depth = transform_stack_.back().layer_depth;
		transform_stack_.push_back(transform_stack_.back() * transform.image_transform);
		auto new_layer_depth = transform_stack_.back().layer_depth;

		if (previous_layer_depth < new_layer_depth)
		{
			layer new_layer(transform_stack_.back().blend_mode);

			if (layer_stack_.empty())
			{
				layers_.push_back(std::move(new_layer));
				layer_stack_.push_back(&layers_.back());
			}
			else
			{
				layer_stack_.back()->sublayers.push_back(std::move(new_layer));
				layer_stack_.push_back(&layer_stack_.back()->sublayers.back());
			}
		}

	}

	void visit(const core::const_frame& frame)
	{
		if(frame.pixel_format_desc().format == core::pixel_format::invalid)
			return;

		if(frame.pixel_format_desc().planes.empty())
			return;

		if(transform_stack_.back().field_mode == core::field_mode::empty)
			return;

		item item;
		item.pix_desc	= frame.pixel_format_desc();
		item.transform	= transform_stack_.back();
		item.geometry	= frame.geometry();

		// NOTE: Once we have copied the arrays they are no longer valid for reading!!! Check for alternative solution e.g. transfer with AMD_pinned_memory.
		for(int n = 0; n < static_cast<int>(item.pix_desc.planes.size()); ++n)
			item.textures.push_back(ogl_->copy_async(frame.image_data(n), item.pix_desc.planes[n].width, item.pix_desc.planes[n].height, item.pix_desc.planes[n].stride, item.transform.use_mipmap));

		layer_stack_.back()->items.push_back(item);
	}

	void pop()
	{
		transform_stack_.pop_back();
		layer_stack_.resize(transform_stack_.back().layer_depth);
	}

	std::future<array<const std::uint8_t>> render(const core::video_format_desc& format_desc, bool straighten_alpha)
	{
		return renderer_(std::move(layers_), format_desc, straighten_alpha);
	}

	core::mutable_frame create_frame(const void* tag, const core::pixel_format_desc& desc, const core::audio_channel_layout& channel_layout) override
	{
		std::vector<array<std::uint8_t>> buffers;
		for (auto& plane : desc.planes)
			buffers.push_back(ogl_->create_array(plane.size));

		return core::mutable_frame(std::move(buffers), core::mutable_audio_buffer(), tag, desc, channel_layout);
	}

	int get_max_frame_size() override
	{
		return ogl_->invoke([]
		{
			GLint64 params[1];
			glGetInteger64v(GL_MAX_TEXTURE_SIZE, params);
			return static_cast<int>(params[0]);
		});
	}
};

image_mixer::image_mixer(const spl::shared_ptr<device>& ogl, bool blend_modes_wanted, bool straight_alpha_wanted, int channel_id) : impl_(new impl(ogl, blend_modes_wanted, straight_alpha_wanted, channel_id)){}
image_mixer::~image_mixer(){}
void image_mixer::push(const core::frame_transform& transform){impl_->push(transform);}
void image_mixer::visit(const core::const_frame& frame){impl_->visit(frame);}
void image_mixer::pop(){impl_->pop();}
int image_mixer::get_max_frame_size() { return impl_->get_max_frame_size(); }
std::future<array<const std::uint8_t>> image_mixer::operator()(const core::video_format_desc& format_desc, bool straighten_alpha){return impl_->render(format_desc, straighten_alpha);}
core::mutable_frame image_mixer::create_frame(const void* tag, const core::pixel_format_desc& desc, const core::audio_channel_layout& channel_layout) {return impl_->create_frame(tag, desc, channel_layout);}

}}}
