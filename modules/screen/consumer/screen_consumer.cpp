﻿/*
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

#include "screen_consumer.h"

#include <GL/glew.h>
#include <SFML/Window.hpp>

#include <common/diagnostics/graph.h>
#include <common/gl/gl_check.h>
#include <common/log.h>
#include <common/memory.h>
#include <common/array.h>
#include <common/memshfl.h>
#include <common/utf.h>
#include <common/prec_timer.h>
#include <common/future.h>
#include <common/timer.h>
#include <common/param.h>
#include <common/os/general_protection_fault.h>
#include <common/scope_exit.h>

//#include <windows.h>

#include <ffmpeg/producer/filter/filter.h>
#include <ffmpeg/producer/util/util.h>

#include <core/video_format.h>
#include <core/frame/frame.h>
#include <core/consumer/frame_consumer.h>
#include <core/interaction/interaction_sink.h>
#include <core/help/help_sink.h>
#include <core/help/help_repository.h>

#include <boost/circular_buffer.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>

#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <vector>

#ifndef _MSC_VER
#include <common/os/linux/x11_check.h>
#endif

#if defined(_MSC_VER)
#pragma warning (push)
#pragma warning (disable : 4244)
#endif
extern "C"
{
	#define __STDC_CONSTANT_MACROS
	#define __STDC_LIMIT_MACROS
	#include <libavcodec/avcodec.h>
	#include <libavutil/imgutils.h>
}
#if defined(_MSC_VER)
#pragma warning (pop)
#endif

namespace caspar { namespace screen {

enum class stretch
{
	none,
	uniform,
	fill,
	uniform_to_fill
};

struct configuration
{
	enum class aspect_ratio
	{
		aspect_4_3 = 0,
		aspect_16_9,
		aspect_invalid,
	};

	std::wstring	name				= L"Screen consumer";
	int				screen_index		= 0;
	screen::stretch	stretch				= screen::stretch::fill;
	bool			windowed			= true;
	bool			auto_deinterlace	= true;
	bool			key_only			= false;
	aspect_ratio	aspect				= aspect_ratio::aspect_invalid;
	bool			vsync				= false;
	bool			interactive			= true;
	bool			borderless			= false;
};

struct screen_consumer : boost::noncopyable
{
	const configuration									config_;
	core::video_format_desc								format_desc_;
	int													channel_index_;

	GLuint												texture_		= 0;
	std::vector<GLuint>									pbos_			= std::vector<GLuint> { 0, 0 };

	float												width_;
	float												height_;
	int													screen_x_;
	int													screen_y_;
	int													screen_width_	= format_desc_.width;
	int													screen_height_	= format_desc_.height;
	int													square_width_	= format_desc_.square_width;
	int													square_height_	= format_desc_.square_height;

	sf::Window											window_;
	tbb::atomic<bool>									polling_event_;
	std::int64_t										pts_;

	spl::shared_ptr<diagnostics::graph>					graph_;
	caspar::timer										perf_timer_;
	caspar::timer										tick_timer_;

	caspar::prec_timer									wait_timer_;

	tbb::concurrent_bounded_queue<core::const_frame>	frame_buffer_;
	core::interaction_sink*								sink_;

	boost::thread										thread_;
	tbb::atomic<bool>									is_running_;
	tbb::atomic<int64_t>								current_presentation_age_;

	ffmpeg::filter										filter_;
public:
	screen_consumer(
			const configuration& config,
			const core::video_format_desc& format_desc,
			int channel_index,
			core::interaction_sink* sink)
		: config_(config)
		, format_desc_(format_desc)
		, channel_index_(channel_index)
		, pts_(0)
		, sink_(sink)
		, filter_([&]() -> ffmpeg::filter
		{
			const auto sample_aspect_ratio =
				boost::rational<int>(
					format_desc.square_width,
					format_desc.square_height) /
				boost::rational<int>(
					format_desc.width,
					format_desc.height);

			return ffmpeg::filter(
				format_desc.width,
				format_desc.height,
				boost::rational<int>(format_desc.duration, format_desc.time_scale),
				boost::rational<int>(format_desc.time_scale, format_desc.duration),
				sample_aspect_ratio,
				AV_PIX_FMT_BGRA,
				{ AV_PIX_FMT_BGRA },
				format_desc.field_mode == core::field_mode::progressive || !config.auto_deinterlace ? "" : "format=pix_fmts=gbrp,YADIF=1:-1");
		}())
	{
		if (format_desc_.format == core::video_format::ntsc && config_.aspect == configuration::aspect_ratio::aspect_4_3)
		{
			// Use default values which are 4:3.
		}
		else
		{
			if (config_.aspect == configuration::aspect_ratio::aspect_16_9)
				square_width_ = (format_desc.height*16)/9;
			else if (config_.aspect == configuration::aspect_ratio::aspect_4_3)
				square_width_ = (format_desc.height*4)/3;
		}

		frame_buffer_.set_capacity(1);

		graph_->set_color("tick-time", diagnostics::color(0.0f, 0.6f, 0.9f));
		graph_->set_color("frame-time", diagnostics::color(0.1f, 1.0f, 0.1f));
		graph_->set_color("dropped-frame", diagnostics::color(0.3f, 0.6f, 0.3f));
		graph_->set_text(print());
		diagnostics::register_graph(graph_);

		/*DISPLAY_DEVICE d_device = {sizeof(d_device), 0};
		std::vector<DISPLAY_DEVICE> displayDevices;
		for(int n = 0; EnumDisplayDevices(NULL, n, &d_device, NULL); ++n)
			displayDevices.push_back(d_device);

		if(config_.screen_index >= displayDevices.size())
			CASPAR_LOG(warning) << print() << L" Invalid screen-index: " << config_.screen_index;

		DEVMODE devmode = {};
		if(!EnumDisplaySettings(displayDevices[config_.screen_index].DeviceName, ENUM_CURRENT_SETTINGS, &devmode))
			CASPAR_LOG(warning) << print() << L" Could not find display settings for screen-index: " << config_.screen_index;

		screen_x_		= devmode.dmPosition.x;
		screen_y_		= devmode.dmPosition.y;
		screen_width_	= config_.windowed ? square_width_ : devmode.dmPelsWidth;
		screen_height_	= config_.windowed ? square_height_ : devmode.dmPelsHeight;*/
		screen_x_		= 0;
		screen_y_		= 0;
		screen_width_	= square_width_;
		screen_height_	= square_height_;

		polling_event_ = false;
		is_running_ = true;
		current_presentation_age_ = 0;
		thread_ = boost::thread([this]{run();});
	}

	~screen_consumer()
	{
		is_running_ = false;
		frame_buffer_.try_push(core::const_frame::empty());
		thread_.join();
	}

	void init()
	{
		auto window_style = config_.borderless
			? sf::Style::None
			: (config_.windowed
				? sf::Style::Resize | sf::Style::Close
				: sf::Style::Fullscreen);
		window_.create(sf::VideoMode::getDesktopMode(), u8(print()), window_style);

		if (config_.windowed)
		{
			window_.setPosition(sf::Vector2i(screen_x_, screen_y_));
			window_.setSize(sf::Vector2u(screen_width_, screen_height_));
		}
		else
		{
			screen_width_	= window_.getSize().x;
			screen_height_	= window_.getSize().y;
		}

		window_.setMouseCursorVisible(config_.interactive);
		window_.setActive();

		if(!GLEW_VERSION_2_1 && glewInit() != GLEW_OK)
			CASPAR_THROW_EXCEPTION(gl::ogl_exception() << msg_info("Failed to initialize GLEW."));

		if(!GLEW_VERSION_2_1)
			CASPAR_THROW_EXCEPTION(not_supported() << msg_info("Missing OpenGL 2.1 support."));

		GL(glEnable(GL_TEXTURE_2D));
		GL(glDisable(GL_DEPTH_TEST));
		GL(glClearColor(0.0, 0.0, 0.0, 0.0));
		GL(glViewport(0, 0, format_desc_.width, format_desc_.height));
		GL(glLoadIdentity());

		calculate_aspect();

		GL(glGenTextures(1, &texture_));
		GL(glBindTexture(GL_TEXTURE_2D, texture_));
		GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP));
		GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP));
		GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, format_desc_.width, format_desc_.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0));
		GL(glBindTexture(GL_TEXTURE_2D, 0));

		GL(glGenBuffers(2, pbos_.data()));

		glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, pbos_[0]);
		glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, format_desc_.size, 0, GL_STREAM_DRAW_ARB);
		glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, pbos_[1]);
		glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, format_desc_.size, 0, GL_STREAM_DRAW_ARB);
		glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

		window_.setVerticalSyncEnabled(config_.vsync);

		if (config_.vsync)
		{
			CASPAR_LOG(info) << print() << " Enabled vsync.";
		}
	}

	void uninit()
	{
		if(texture_)
			glDeleteTextures(1, &texture_);

		for (auto& pbo : pbos_)
		{
			if(pbo)
				glDeleteBuffers(1, &pbo);
		}
	}

	void run()
	{
		ensure_gpf_handler_installed_for_thread("screen-consumer-thread");

		try
		{
			init();

			while(is_running_)
			{
				try
				{
					auto poll_event = [this](sf::Event& e)
					{
						polling_event_ = true;
						CASPAR_SCOPE_EXIT
						{
							polling_event_ = false;
						};
						return window_.pollEvent(e);
					};

					sf::Event e;
					while(poll_event(e))
					{
						if (e.type == sf::Event::Resized)
							calculate_aspect();
						else if (e.type == sf::Event::Closed)
							is_running_ = false;
						else if (config_.interactive && sink_)
						{
							switch (e.type)
							{
							case sf::Event::MouseMoved:
								{
									auto& mouse_move = e.mouseMove;
									sink_->on_interaction(spl::make_shared<core::mouse_move_event>(
											1,
											static_cast<double>(mouse_move.x) / screen_width_,
											static_cast<double>(mouse_move.y) / screen_height_));
								}
								break;
							case sf::Event::MouseButtonPressed:
							case sf::Event::MouseButtonReleased:
								{
									auto& mouse_button = e.mouseButton;
									sink_->on_interaction(spl::make_shared<core::mouse_button_event>(
											1,
											static_cast<double>(mouse_button.x) / screen_width_,
											static_cast<double>(mouse_button.y) / screen_height_,
											static_cast<int>(mouse_button.button),
											e.type == sf::Event::MouseButtonPressed));
								}
								break;
							case sf::Event::MouseWheelMoved:
								{
									auto& wheel_moved = e.mouseWheel;
									sink_->on_interaction(spl::make_shared<core::mouse_wheel_event>(
											1,
											static_cast<double>(wheel_moved.x) / screen_width_,
											static_cast<double>(wheel_moved.y) / screen_height_,
											wheel_moved.delta));
								}
								break;
							}
						}
					}

					core::const_frame frame;
					frame_buffer_.pop(frame);

					render_and_draw_frame(frame);

					/*perf_timer_.restart();
					render(frame);
					graph_->set_value("frame-time", perf_timer_.elapsed()*format_desc_.fps*0.5);

					window_.Display();*/

					current_presentation_age_ = frame.get_age_millis();
					graph_->set_value("tick-time", tick_timer_.elapsed()*format_desc_.fps*0.5);
					tick_timer_.restart();
				}
				catch(...)
				{
					CASPAR_LOG_CURRENT_EXCEPTION();
					is_running_ = false;
				}
			}

			uninit();
		}
		catch(...)
		{
			CASPAR_LOG_CURRENT_EXCEPTION();
		}
	}

	void try_sleep_almost_until_vblank()
	{
		static const double THRESHOLD = 0.003;
		double threshold = config_.vsync ? THRESHOLD : 0.0;

		auto frame_time = 1.0 / (format_desc_.fps * format_desc_.field_count);

		wait_timer_.tick(frame_time - threshold);
	}

	void wait_for_vblank_and_display()
	{
		try_sleep_almost_until_vblank();
		window_.display();
		// Make sure that the next tick measures the duration from this point in time.
		wait_timer_.tick(0.0);
	}

	spl::shared_ptr<AVFrame> get_av_frame()
	{
		auto av_frame = ffmpeg::create_frame();

		av_frame->linesize[0]		= format_desc_.width*4;
		av_frame->format				= AVPixelFormat::AV_PIX_FMT_BGRA;
		av_frame->width				= format_desc_.width;
		av_frame->height				= format_desc_.height;
		av_frame->interlaced_frame	= format_desc_.field_mode != core::field_mode::progressive;
		av_frame->top_field_first	= format_desc_.field_mode == core::field_mode::upper ? 1 : 0;
		av_frame->pts				= pts_++;

		return av_frame;
	}

	void render_and_draw_frame(core::const_frame input_frame)
	{
		if(static_cast<size_t>(input_frame.image_data().size()) != format_desc_.size)
			return;

		if(screen_width_ == 0 && screen_height_ == 0)
			return;

		perf_timer_.restart();
		auto av_frame = get_av_frame();
		av_frame->data[0] = const_cast<uint8_t*>(input_frame.image_data().begin());

		filter_.push(av_frame);
		auto frame = filter_.poll();

		if (!frame)
			return;

		if (!filter_.is_double_rate())
		{
			render(spl::make_shared_ptr(frame));
			graph_->set_value("frame-time", perf_timer_.elapsed() * format_desc_.fps * 0.5);

			wait_for_vblank_and_display(); // progressive frame
		}
		else
		{
			render(spl::make_shared_ptr(frame));
			double perf_elapsed = perf_timer_.elapsed();

			wait_for_vblank_and_display(); // field1

			perf_timer_.restart();
			frame = filter_.poll();
			render(spl::make_shared_ptr(frame));
			perf_elapsed += perf_timer_.elapsed();
			graph_->set_value("frame-time", perf_elapsed * format_desc_.fps * 0.5);

			wait_for_vblank_and_display(); // field2
		}
	}

	void render(spl::shared_ptr<AVFrame> av_frame)
	{
		CASPAR_LOG_CALL(trace) << "screen_consumer::render() <- " << print();
		GL(glBindTexture(GL_TEXTURE_2D, texture_));

		GL(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos_[0]));
		GL(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, format_desc_.width, format_desc_.height, GL_BGRA, GL_UNSIGNED_BYTE, 0));

		GL(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos_[1]));
		GL(glBufferData(GL_PIXEL_UNPACK_BUFFER, format_desc_.size, 0, GL_STREAM_DRAW));

		auto ptr = reinterpret_cast<char*>(GL2(glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY)));
		if(ptr)
		{
			if(config_.key_only)
			{
				tbb::parallel_for(tbb::blocked_range<int>(0, format_desc_.height), [&](const tbb::blocked_range<int>& r)
				{
					for(int n = r.begin(); n != r.end(); ++n)
						aligned_memshfl(ptr+n*format_desc_.width*4, av_frame->data[0]+n*av_frame->linesize[0], format_desc_.width*4, 0x0F0F0F0F, 0x0B0B0B0B, 0x07070707, 0x03030303);
				});
			}
			else
			{
				std::memcpy(ptr, av_frame->data[0], format_desc_.size);
			}

			GL(glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER)); // release the mapped buffer
		}

		GL(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0));

		GL(glClear(GL_COLOR_BUFFER_BIT));
		glBegin(GL_QUADS);
				glTexCoord2f(0.0f,	  1.0f);	glVertex2f(-width_, -height_);
				glTexCoord2f(1.0f,	  1.0f);	glVertex2f( width_, -height_);
				glTexCoord2f(1.0f,	  0.0f);	glVertex2f( width_,  height_);
				glTexCoord2f(0.0f,	  0.0f);	glVertex2f(-width_,  height_);
		glEnd();

		GL(glBindTexture(GL_TEXTURE_2D, 0));

		std::rotate(pbos_.begin(), pbos_.begin() + 1, pbos_.end());
	}


	std::future<bool> send(core::const_frame frame)
	{
		if(!frame_buffer_.try_push(frame) && !polling_event_)
			graph_->set_tag(diagnostics::tag_severity::WARNING, "dropped-frame");

		return make_ready_future(is_running_.load());
	}

	std::wstring channel_and_format() const
	{
		return L"[" + boost::lexical_cast<std::wstring>(channel_index_) + L"|" + format_desc_.name + L"]";
	}

	std::wstring print() const
	{
		return config_.name + L" " + channel_and_format();
	}

	void calculate_aspect()
	{
		if(config_.windowed)
		{
			screen_height_ = window_.getSize().y;
			screen_width_ = window_.getSize().x;
		}

		GL(glViewport(0, 0, screen_width_, screen_height_));

		std::pair<float, float> target_ratio = None();
		if (config_.stretch == screen::stretch::fill)
			target_ratio = Fill();
		else if (config_.stretch == screen::stretch::uniform)
			target_ratio = Uniform();
		else if (config_.stretch == screen::stretch::uniform_to_fill)
			target_ratio = UniformToFill();

		width_ = target_ratio.first;
		height_ = target_ratio.second;
	}

	std::pair<float, float> None()
	{
		float width = static_cast<float>(square_width_)/static_cast<float>(screen_width_);
		float height = static_cast<float>(square_height_)/static_cast<float>(screen_height_);

		return std::make_pair(width, height);
	}

	std::pair<float, float> Uniform()
	{
		float aspect = static_cast<float>(square_width_)/static_cast<float>(square_height_);
		float width = std::min(1.0f, static_cast<float>(screen_height_)*aspect/static_cast<float>(screen_width_));
		float height = static_cast<float>(screen_width_*width)/static_cast<float>(screen_height_*aspect);

		return std::make_pair(width, height);
	}

	std::pair<float, float> Fill()
	{
		return std::make_pair(1.0f, 1.0f);
	}

	std::pair<float, float> UniformToFill()
	{
		float wr = static_cast<float>(square_width_)/static_cast<float>(screen_width_);
		float hr = static_cast<float>(square_height_)/static_cast<float>(screen_height_);
		float r_inv = 1.0f/std::min(wr, hr);

		float width = wr*r_inv;
		float height = hr*r_inv;

		return std::make_pair(width, height);
	}
};


struct screen_consumer_proxy : public core::frame_consumer
{
	core::monitor::subject				monitor_subject_;
	const configuration					config_;
	std::unique_ptr<screen_consumer>	consumer_;
	core::interaction_sink*				sink_;

public:

	screen_consumer_proxy(const configuration& config, core::interaction_sink* sink)
		: config_(config)
		, sink_(sink)
	{
	}

	// frame_consumer

	void initialize(const core::video_format_desc& format_desc, const core::audio_channel_layout&, int channel_index) override
	{
		consumer_.reset();
		consumer_.reset(new screen_consumer(config_, format_desc, channel_index, sink_));
	}

	int64_t presentation_frame_age_millis() const override
	{
		return consumer_ ? static_cast<int64_t>(consumer_->current_presentation_age_) : 0;
	}

	std::future<bool> send(core::const_frame frame) override
	{
		return consumer_->send(frame);
	}

	std::wstring print() const override
	{
		return consumer_ ? consumer_->print() : L"[screen_consumer]";
	}

	std::wstring name() const override
	{
		return L"screen";
	}

	boost::property_tree::wptree info() const override
	{
		boost::property_tree::wptree info;
		info.add(L"type", L"screen");
		info.add(L"key-only", config_.key_only);
		info.add(L"windowed", config_.windowed);
		info.add(L"auto-deinterlace", config_.auto_deinterlace);
		info.add(L"vsync", config_.vsync);
		return info;
	}

	bool has_synchronization_clock() const override
	{
		return false;
	}

	int buffer_depth() const override
	{
		return 1;
	}

	int index() const override
	{
		return 600 + (config_.key_only ? 10 : 0) + config_.screen_index;
	}

	core::monitor::subject& monitor_output()
	{
		return monitor_subject_;
	}
};

void describe_consumer(core::help_sink& sink, const core::help_repository& repo)
{
	sink.short_description(L"Displays the contents of a channel on screen using OpenGL.");
	sink.syntax(
			L"SCREEN "
			L"{[screen_index:int]|1} "
			L"{[fullscreen:FULLSCREEN]} "
			L"{[borderless:BORDERLESS]} "
			L"{[key_only:KEY_ONLY]} "
			L"{[non_interactive:NON_INTERACTIVE]} "
			L"{[no_auto_deinterlace:NO_AUTO_DEINTERLACE]} "
			L"{NAME [name:string]}");
	sink.para()->text(L"Displays the contents of a channel on screen using OpenGL.");
	sink.definitions()
		->item(L"screen_index", L"Determines which screen the channel should be displayed on. Defaults to 1.")
		->item(L"fullscreen", L"If specified opens the window in fullscreen.")
		->item(L"borderless", L"Makes the window appear without any window decorations.")
		->item(L"key_only", L"Only displays the alpha channel of the video channel if specified.")
		->item(L"non_interactive", L"If specified does not send mouse input to producers on the video channel.")
		->item(L"no_auto_deinterlace", L"If the video mode of the channel is an interlaced mode, specifying this will turn of deinterlacing.")
		->item(L"name", L"Optionally specifies a name of the window to show.");
	sink.para()->text(L"Examples:");
	sink.example(L">> ADD 1 SCREEN", L"opens a screen consumer on the default screen.");
	sink.example(L">> ADD 1 SCREEN 2", L"opens a screen consumer on the screen 2.");
	sink.example(L">> ADD 1 SCREEN 1 FULLSCREEN", L"opens a screen consumer in fullscreen on screen 1.");
	sink.example(L">> ADD 1 SCREEN 1 BORDERLESS", L"opens a screen consumer without borders/window decorations on screen 1.");
}

spl::shared_ptr<core::frame_consumer> create_consumer(
		const std::vector<std::wstring>& params, core::interaction_sink* sink, std::vector<spl::shared_ptr<core::video_channel>> channels)
{
	if (params.size() < 1 || !boost::iequals(params.at(0), L"SCREEN"))
		return core::frame_consumer::empty();

#ifndef _MSC_VER
	if (!caspar::can_open_display()){
		CASPAR_LOG(error) << L"Cannot initialise screen consumer without xserver";
		return core::frame_consumer::empty();
	}
#endif

	configuration config;

	if (params.size() > 1)
		config.screen_index = boost::lexical_cast<int>(params.at(1));

	config.windowed			= !contains_param(L"FULLSCREEN", params);
	config.key_only			=  contains_param(L"KEY_ONLY", params);
	config.interactive		= !contains_param(L"NON_INTERACTIVE", params);
	config.auto_deinterlace	= !contains_param(L"NO_AUTO_DEINTERLACE", params);
	config.borderless		=  contains_param(L"BORDERLESS", params);

	if (contains_param(L"NAME", params))
		config.name = get_param(L"NAME", params);

	return spl::make_shared<screen_consumer_proxy>(config, sink);
}

spl::shared_ptr<core::frame_consumer> create_preconfigured_consumer(
		const boost::property_tree::wptree& ptree, core::interaction_sink* sink, std::vector<spl::shared_ptr<core::video_channel>> channels)
{
#ifndef _MSC_VER
        if (!caspar::can_open_display()){
                CASPAR_LOG(error) << L"Cannot initialise screen consumer without xserver";
                return core::frame_consumer::empty();
        }
#endif

	configuration config;
	config.name				= ptree.get(L"name",				config.name);
	config.screen_index		= ptree.get(L"device",				config.screen_index + 1) - 1;
	config.windowed			= ptree.get(L"windowed",			config.windowed);
	config.key_only			= ptree.get(L"key-only",			config.key_only);
	config.auto_deinterlace	= ptree.get(L"auto-deinterlace",	config.auto_deinterlace);
	config.vsync			= ptree.get(L"vsync",				config.vsync);
	config.interactive		= ptree.get(L"interactive",			config.interactive);
	config.borderless		= ptree.get(L"borderless",			config.borderless);

	auto stretch_str = ptree.get(L"stretch", L"default");
	if(stretch_str == L"uniform")
		config.stretch = screen::stretch::uniform;
	else if(stretch_str == L"uniform_to_fill")
		config.stretch = screen::stretch::uniform_to_fill;

	auto aspect_str = ptree.get(L"aspect-ratio", L"default");
	if(aspect_str == L"16:9")
		config.aspect = configuration::aspect_ratio::aspect_16_9;
	else if(aspect_str == L"4:3")
		config.aspect = configuration::aspect_ratio::aspect_4_3;

	return spl::make_shared<screen_consumer_proxy>(config, sink);
}

}}
