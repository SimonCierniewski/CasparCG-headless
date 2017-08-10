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

// TODO: Smart GC

#include "../../StdAfx.h"

#include "device.h"

#include "buffer.h"
#include "texture.h"
#include "shader.h"

#include <common/assert.h>
#include <common/except.h>
#include <common/future.h>
#include <common/array.h>
#include <common/memory.h>
#include <common/gl/gl_check.h>
#include <common/gl/egl_check.h>
#include <common/timer.h>

#include <GL/glew.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_queue.h>

#include <boost/utility/declval.hpp>
#include <boost/property_tree/ptree.hpp>

#include <array>
#include <unordered_map>

#include <tbb/parallel_for.h>

namespace caspar { namespace accelerator { namespace ogl {
		
struct device::impl : public std::enable_shared_from_this<impl>
{	
	static_assert(std::is_same<decltype(boost::declval<device>().impl_), spl::shared_ptr<impl>>::value, "impl_ must be shared_ptr");

	tbb::concurrent_hash_map<buffer*, std::shared_ptr<texture>> texture_cache_;

	EGLDisplay  m_display; ///< The internal EGL display
	EGLContext  m_context; ///< The internal EGL context
	EGLSurface  m_surface; ///< The internal EGL surface
	EGLConfig   m_config;  ///< The internal EGL config
	
	std::array<tbb::concurrent_unordered_map<std::size_t, tbb::concurrent_bounded_queue<std::shared_ptr<texture>>>, 8>	device_pools_;
	std::array<tbb::concurrent_unordered_map<std::size_t, tbb::concurrent_bounded_queue<std::shared_ptr<buffer>>>, 2>	host_pools_;
	
	GLuint fbo_;

	executor& executor_;
				
	impl(executor& executor) 
		: executor_(executor),
		m_display (EGL_NO_DISPLAY),
		m_context (EGL_NO_CONTEXT),
		m_surface (EGL_NO_SURFACE),
		m_config  (NULL)
	{
		executor_.set_capacity(256);

		CASPAR_LOG(info) << L"Initializing OpenGL Device.";
		
		executor_.invoke([=]
		{
			resetEGL();
			initEGL();
			eglCheck(eglMakeCurrent(m_display, m_surface, m_surface, m_context));
						
			if (glewInit() != GLEW_OK)
				CASPAR_THROW_EXCEPTION(gl::ogl_exception() << msg_info("Failed to initialize GLEW."));
		
			if(!GLEW_VERSION_3_0)
				CASPAR_THROW_EXCEPTION(not_supported() << msg_info("Your graphics card does not meet the minimum hardware requirements since it does not support OpenGL 3.0 or higher."));
	
			glGenFramebuffers(1, &fbo_);				
			glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
		});
				
		CASPAR_LOG(info) << L"Successfully initialized OpenGL " << version();
	}

	~impl()
	{
		auto context = executor_.is_current() ? std::string() : get_context();

		executor_.invoke([=]
		{
			CASPAR_SCOPED_CONTEXT_MSG(context);
			texture_cache_.clear();

			for (auto& pool : host_pools_)
				pool.clear();

			for (auto& pool : device_pools_)
				pool.clear();

			glDeleteFramebuffers(1, &fbo_);

			resetEGL();
		});
	}

	void resetEGL()
	{
		// Deactivate the current context
		EGLContext currentContext = eglCheck(eglGetCurrentContext());

		if (currentContext == m_context)
			eglCheck(eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));

		// Destroy context
		if (m_context != EGL_NO_CONTEXT)
			eglCheck(eglDestroyContext(m_display, m_context));

		// Destroy surface
		if (m_surface != EGL_NO_SURFACE)
			eglCheck(eglDestroySurface(m_display, m_surface));

		if (m_display != EGL_NO_DISPLAY)
			eglTerminate(m_display);
	}

	void initEGL()
	{
		static EGLDisplay display = EGL_NO_DISPLAY;
		display = eglCheck(eglGetDisplay(EGL_DEFAULT_DISPLAY));

		eglCheck(eglInitialize(display, NULL, NULL));
		m_display = display;

		const EGLint attributes[] = {
			EGL_BLUE_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_RED_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
		        EGL_DEPTH_SIZE, EGL_DONT_CARE,
//			EGL_STENCIL_SIZE, 0,
//			EGL_SAMPLE_BUFFERS, 0,
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_NONE
		};

		EGLint configCount;
		EGLConfig configs[1];

		// Ask EGL for the best config matching our video settings
		// TODO - seems to always return 0
		//eglCheck(eglChooseConfig(m_display, attributes, configs, 1, &configCount));
		eglCheck(eglGetConfigs(m_display, configs, 1, &configCount));
		m_config = configs[0];

		eglCheck(eglBindAPI(EGL_OPENGL_API));

		EGLint attrib_list[] = {
		        EGL_WIDTH, 1,
		        EGL_HEIGHT,1,
		        EGL_NONE
		};

		m_surface = eglCheck(eglCreatePbufferSurface(m_display, m_config, attrib_list));

		const EGLint contextVersion[] = {
			EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
			EGL_CONTEXT_MINOR_VERSION_KHR, 0,
		        EGL_CONTEXT_CLIENT_VERSION, 1,
		        EGL_NONE
		};

		EGLContext toShared;
		eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		m_context = eglCheck(eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextVersion));
	}

	boost::property_tree::wptree info() const
	{
		boost::property_tree::wptree info;

		boost::property_tree::wptree pooled_device_buffers;
		size_t total_pooled_device_buffer_size	= 0;
		size_t total_pooled_device_buffer_count	= 0;

		for (size_t i = 0; i < device_pools_.size(); ++i)
		{
			auto& pools		= device_pools_.at(i);
			bool mipmapping	= i > 3;
			auto stride		= mipmapping ? i - 3 : i + 1;

			for (auto& pool : pools)
			{
				auto width	= pool.first >> 16;
				auto height	= pool.first & 0x0000FFFF;
				auto size	= width * height * stride;
				auto count	= pool.second.size();

				if (count == 0)
					continue;

				boost::property_tree::wptree pool_info;

				pool_info.add(L"stride",		stride);
				pool_info.add(L"mipmapping",	mipmapping);
				pool_info.add(L"width",			width);
				pool_info.add(L"height",		height);
				pool_info.add(L"size",			size);
				pool_info.add(L"count",			count);

				total_pooled_device_buffer_size		+= size * count;
				total_pooled_device_buffer_count	+= count;

				pooled_device_buffers.add_child(L"device_buffer_pool", pool_info);
			}
		}

		info.add_child(L"gl.details.pooled_device_buffers", pooled_device_buffers);

		boost::property_tree::wptree pooled_host_buffers;
		size_t total_read_size		= 0;
		size_t total_write_size		= 0;
		size_t total_read_count		= 0;
		size_t total_write_count	= 0;

		for (size_t i = 0; i < host_pools_.size(); ++i)
		{
			auto& pools	= host_pools_.at(i);
			auto usage	= static_cast<buffer::usage>(i);

			for (auto& pool : pools)
			{
				auto size	= pool.first;
				auto count	= pool.second.size();

				if (count == 0)
					continue;

				boost::property_tree::wptree pool_info;

				pool_info.add(L"usage",	usage == buffer::usage::read_only ? L"read_only" : L"write_only");
				pool_info.add(L"size",	size);
				pool_info.add(L"count",	count);

				pooled_host_buffers.add_child(L"host_buffer_pool", pool_info);

				(usage == buffer::usage::read_only ? total_read_count : total_write_count) += count;
				(usage == buffer::usage::read_only ? total_read_size : total_write_size) += size * count;
			}
		}

		info.add_child(L"gl.details.pooled_host_buffers",				pooled_host_buffers);
		info.add(L"gl.summary.pooled_device_buffers.total_count",		total_pooled_device_buffer_count);
		info.add(L"gl.summary.pooled_device_buffers.total_size",		total_pooled_device_buffer_size);
		info.add_child(L"gl.summary.all_device_buffers",				texture::info());
		info.add(L"gl.summary.pooled_host_buffers.total_read_count",	total_read_count);
		info.add(L"gl.summary.pooled_host_buffers.total_write_count",	total_write_count);
		info.add(L"gl.summary.pooled_host_buffers.total_read_size",		total_read_size);
		info.add(L"gl.summary.pooled_host_buffers.total_write_size",	total_write_size);
		info.add_child(L"gl.summary.all_host_buffers",					buffer::info());

		return info;
	}
		
	std::wstring version()
	{	
		try
		{
			return executor_.invoke([]
			{
				return u16(reinterpret_cast<const char*>(GL2(glGetString(GL_VERSION)))) + L" " + u16(reinterpret_cast<const char*>(GL2(glGetString(GL_VENDOR))));
			});	
		}
		catch(...)
		{
			return L"Not found";;
		}
	}
							
	spl::shared_ptr<texture> create_texture(int width, int height, int stride, bool mipmapped, bool clear)
	{
		CASPAR_VERIFY(stride > 0 && stride < 5);
		CASPAR_VERIFY(width > 0 && height > 0);

		if(!executor_.is_current())
			CASPAR_THROW_EXCEPTION(invalid_operation() << msg_info("Operation only valid in an OpenGL Context."));
					
		auto pool = &device_pools_[stride - 1 + (mipmapped ? 4 : 0)][((width << 16) & 0xFFFF0000) | (height & 0x0000FFFF)];
		
		std::shared_ptr<texture> tex;
		if(!pool->try_pop(tex))		
			tex = spl::make_shared<texture>(width, height, stride, mipmapped);
	
		if(clear)
			tex->clear();

		return spl::shared_ptr<texture>(tex.get(), [tex, pool](texture*) mutable
		{		
			pool->push(tex);	
		});
	}
		
	spl::shared_ptr<buffer> create_buffer(std::size_t size, buffer::usage usage)
	{
		CASPAR_VERIFY(size > 0);
		
		auto pool = &host_pools_[static_cast<int>(usage)][size];
		
		std::shared_ptr<buffer> buf;
		if(!pool->try_pop(buf))	
		{
			caspar::timer timer;

			auto context = executor_.is_current() ? std::string() : get_context();

			buf = executor_.invoke([&]
			{
				CASPAR_SCOPED_CONTEXT_MSG(context);
				return std::make_shared<buffer>(size, usage);
			}, task_priority::high_priority);
			
			if(timer.elapsed() > 0.02)
				CASPAR_LOG(warning) << L"[ogl-device] Performance warning. Buffer allocation blocked: " << timer.elapsed();
		}
		
		std::weak_ptr<impl> self = shared_from_this(); // buffers can leave the device context, take a hold on life-time.
		return spl::shared_ptr<buffer>(buf.get(), [=](buffer*) mutable
		{
			auto strong = self.lock();

			if (strong)
			{
				auto context = executor_.is_current() ? std::string() : get_context();

				strong->executor_.invoke([&]
				{
					CASPAR_SCOPED_CONTEXT_MSG(context);
					strong->texture_cache_.erase(buf.get());
				}, task_priority::high_priority);
				
				pool->push(buf);
			}
			else
			{
				CASPAR_LOG(info) << L"Buffer outlived ogl device";
			}
		});
	}

	array<std::uint8_t> create_array(std::size_t size)
	{		
		auto buf = create_buffer(size, buffer::usage::write_only);
		return array<std::uint8_t>(buf->data(), buf->size(), false, buf);
	}

	template<typename T>
	std::shared_ptr<buffer> copy_to_buf(const T& source)
	{
		std::shared_ptr<buffer> buf;

		auto tmp = source.template storage<spl::shared_ptr<buffer>>();
		if(tmp)
			buf = *tmp;
		else
		{			
			buf = create_buffer(source.size(), buffer::usage::write_only);
			tbb::parallel_for(tbb::blocked_range<std::size_t>(0, source.size()), [&](const tbb::blocked_range<std::size_t>& r)
			{
				std::memcpy(buf->data() + r.begin(), source.data() + r.begin(), r.size());
			});
		}

		return buf;
	}

	// TODO: Since the returned texture is cached it SHOULD NOT be modified.
	std::future<std::shared_ptr<texture>> copy_async(const array<const std::uint8_t>& source, int width, int height, int stride, bool mipmapped)
	{
		std::shared_ptr<buffer> buf = copy_to_buf(source);
		auto context = executor_.is_current() ? std::string() : get_context();

		return executor_.begin_invoke([=]() -> std::shared_ptr<texture>
		{
			CASPAR_SCOPED_CONTEXT_MSG(context);
			tbb::concurrent_hash_map<buffer*, std::shared_ptr<texture>>::const_accessor a;
			if(texture_cache_.find(a, buf.get()))
				return spl::make_shared_ptr(a->second);

			auto texture = create_texture(width, height, stride, mipmapped, false);
			texture->copy_from(*buf);

			texture_cache_.insert(std::make_pair(buf.get(), texture));
			
			return texture;
		}, task_priority::high_priority);
	}
	
	std::future<std::shared_ptr<texture>> copy_async(const array<std::uint8_t>& source, int width, int height, int stride, bool mipmapped)
	{
		std::shared_ptr<buffer> buf = copy_to_buf(source);
		auto context = executor_.is_current() ? std::string() : get_context();

		return executor_.begin_invoke([=]() -> std::shared_ptr<texture>
		{
			CASPAR_SCOPED_CONTEXT_MSG(context);
			auto texture = create_texture(width, height, stride, mipmapped, false);
			texture->copy_from(*buf);	
			
			return texture;
		}, task_priority::high_priority);
	}

	std::future<array<const std::uint8_t>> copy_async(const spl::shared_ptr<texture>& source)
	{
		if(!executor_.is_current())
			CASPAR_THROW_EXCEPTION(invalid_operation() << msg_info("Operation only valid in an OpenGL Context."));

		auto buffer = create_buffer(source->size(), buffer::usage::read_only); 
		source->copy_to(*buffer);	

		auto self = shared_from_this();
		auto context = get_context();
		auto cmd = [self, buffer, context]() mutable -> array<const std::uint8_t>
		{
			self->executor_.invoke([&buffer, &context] // Defer blocking "map" call until data is needed.
			{
				CASPAR_LOG_CALL(trace) << "Readback <- " << context;
				buffer->map();
			});
			return array<const std::uint8_t>(buffer->data(), buffer->size(), true, buffer);
		};
		return std::async(std::launch::deferred, std::move(cmd));
	}

	std::future<void> gc()
	{
		return executor_.begin_invoke([=]
		{
			CASPAR_LOG(info) << " ogl: Running GC.";

			try
			{
				for (auto& pools : device_pools_)
				{
					for (auto& pool : pools)
						pool.second.clear();
				}
				for (auto& pools : host_pools_)
				{
					for (auto& pool : pools)
						pool.second.clear();
				}
			}
			catch (...)
			{
				CASPAR_LOG_CURRENT_EXCEPTION();
			}
		}, task_priority::high_priority);
	}
};

device::device() 
	: executor_(L"OpenGL Rendering Context")
	, impl_(new impl(executor_)){}
device::~device(){}
spl::shared_ptr<texture>					device::create_texture(int width, int height, int stride, bool mipmapped){ return impl_->create_texture(width, height, stride, mipmapped, true); }
array<std::uint8_t>							device::create_array(int size){return impl_->create_array(size);}
std::future<std::shared_ptr<texture>>		device::copy_async(const array<const std::uint8_t>& source, int width, int height, int stride, bool mipmapped){return impl_->copy_async(source, width, height, stride, mipmapped);}
std::future<std::shared_ptr<texture>>		device::copy_async(const array<std::uint8_t>& source, int width, int height, int stride, bool mipmapped){ return impl_->copy_async(source, width, height, stride, mipmapped); }
std::future<array<const std::uint8_t>>		device::copy_async(const spl::shared_ptr<texture>& source){return impl_->copy_async(source);}
std::future<void>							device::gc() { return impl_->gc(); }
boost::property_tree::wptree				device::info() const { return impl_->info(); }
std::wstring								device::version() const{return impl_->version();}


}}}


