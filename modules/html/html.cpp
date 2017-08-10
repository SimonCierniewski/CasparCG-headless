/*
* Copyright 2013 Sveriges Television AB http://casparcg.com/
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

#include "html.h"

#include "producer/html_producer.h"
#include "producer/html_cg_proxy.h"

#include <common/executor.h>
#include <common/future.h>
#include <common/env.h>

#include <core/producer/cg_proxy.h>

#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <boost/timer.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/thread/future.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>

#include <map>

#pragma warning(push)
#pragma warning(disable: 4458)
#include <cef_app.h>
#include <cef_version.h>
#pragma warning(pop)

#pragma comment(lib, "libcef.lib")
#pragma comment(lib, "libcef_dll_wrapper.lib")

namespace caspar { namespace html {

std::unique_ptr<executor> g_cef_executor;

void caspar_log(
		const CefRefPtr<CefBrowser>& browser,
		boost::log::trivial::severity_level level,
		const std::string& message)
{
	if (browser)
	{
		auto msg = CefProcessMessage::Create(LOG_MESSAGE_NAME);
		msg->GetArgumentList()->SetInt(0, level);
		msg->GetArgumentList()->SetString(1, message);
		browser->SendProcessMessage(PID_BROWSER, msg);
	}
}

class remove_handler : public CefV8Handler
{
	CefRefPtr<CefBrowser> browser_;
public:
	remove_handler(CefRefPtr<CefBrowser> browser)
		: browser_(browser)
	{
	}

	bool Execute(
			const CefString& name,
			CefRefPtr<CefV8Value> object,
			const CefV8ValueList& arguments,
			CefRefPtr<CefV8Value>& retval,
			CefString& exception) override
	{
		if (!CefCurrentlyOn(TID_RENDERER))
			return false;

		browser_->SendProcessMessage(
				PID_BROWSER,
				CefProcessMessage::Create(REMOVE_MESSAGE_NAME));

		return true;
	}

	IMPLEMENT_REFCOUNTING(remove_handler);
};

class renderer_application : public CefApp, CefRenderProcessHandler
{
	std::vector<CefRefPtr<CefV8Context>> contexts_;
public:
	CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override
	{
		return this;
	}

	void OnContextCreated(
			CefRefPtr<CefBrowser> browser,
			CefRefPtr<CefFrame> frame,
			CefRefPtr<CefV8Context> context) override
	{
		caspar_log(browser, boost::log::trivial::trace,
				"context for frame "
				+ boost::lexical_cast<std::string>(frame->GetIdentifier())
				+ " created");
		contexts_.push_back(context);

		auto window = context->GetGlobal();

		window->SetValue(
				"remove",
				CefV8Value::CreateFunction(
						"remove",
						new remove_handler(browser)),
				V8_PROPERTY_ATTRIBUTE_NONE);

		CefRefPtr<CefV8Value> ret;
		CefRefPtr<CefV8Exception> exception;
		bool injected = context->Eval(R"(
			var requestedAnimationFrames	= {};
			var currentAnimationFrameId		= 0;

			window.requestAnimationFrame = function(callback) {
				requestedAnimationFrames[++currentAnimationFrameId] = callback;
				return currentAnimationFrameId;
			}

			window.cancelAnimationFrame = function(animationFrameId) {
				delete requestedAnimationFrames[animationFrameId];
			}

			function tickAnimations() {
				var requestedFrames = requestedAnimationFrames;
				var timestamp = performance.now();
				requestedAnimationFrames = {};

				for (var animationFrameId in requestedFrames)
					if (requestedFrames.hasOwnProperty(animationFrameId))
						requestedFrames[animationFrameId](timestamp);
			}
		)", ret, exception);

		if (!injected)
			caspar_log(browser, boost::log::trivial::error, "Could not inject javascript animation code.");
	}

	void OnContextReleased(
			CefRefPtr<CefBrowser> browser,
			CefRefPtr<CefFrame> frame,
			CefRefPtr<CefV8Context> context)
	{
		auto removed = boost::remove_if(
				contexts_, [&](const CefRefPtr<CefV8Context>& c)
				{
					return c->IsSame(context);
				});

		if (removed != contexts_.end())
			caspar_log(browser, boost::log::trivial::trace,
					"context for frame "
					+ boost::lexical_cast<std::string>(frame->GetIdentifier())
					+ " released");
		else
			caspar_log(browser, boost::log::trivial::warning,
					"context for frame "
					+ boost::lexical_cast<std::string>(frame->GetIdentifier())
					+ " released, but not found");
	}

	void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) override
	{
		contexts_.clear();
	}

	bool OnProcessMessageReceived(
			CefRefPtr<CefBrowser> browser,
			CefProcessId source_process,
			CefRefPtr<CefProcessMessage> message) override
	{
		if (message->GetName().ToString() == TICK_MESSAGE_NAME)
		{
			for (auto& context : contexts_)
			{
				CefRefPtr<CefV8Value> ret;
				CefRefPtr<CefV8Exception> exception;
				context->Eval("tickAnimations()", ret, exception);
			}

			return true;
		}
		else
		{
			return false;
		}
	}

	IMPLEMENT_REFCOUNTING(renderer_application);
};

bool intercept_command_line(int argc, char** argv)
{
#ifdef _WIN32
	CefMainArgs main_args;
#else
	CefMainArgs main_args(argc, argv);
#endif

	if (CefExecuteProcess(main_args, CefRefPtr<CefApp>(new renderer_application), nullptr) >= 0)
		return true;

	return false;
}

void init(core::module_dependencies dependencies)
{
	dependencies.producer_registry->register_producer_factory(L"HTML Producer", html::create_producer, html::describe_producer);
	
	CefMainArgs main_args;
	g_cef_executor.reset(new executor(L"cef"));
	g_cef_executor->invoke([&]
	{
		CefSettings settings;
		settings.no_sandbox = true;
		settings.remote_debugging_port = env::properties().get(L"configuration.html.remote-debugging-port", 0);
		//settings.windowless_rendering_enabled = true;
		CefInitialize(main_args, settings, nullptr, nullptr);
	});
	g_cef_executor->begin_invoke([&]
	{
		CefRunMessageLoop();
	});
	dependencies.cg_registry->register_cg_producer(
			L"html",
			{ L".html" },
			[](const std::wstring& filename)
			{
				return "";
			},
			[](const spl::shared_ptr<core::frame_producer>& producer)
			{
				return spl::make_shared<html_cg_proxy>(producer);
			},
			[](const core::frame_producer_dependencies& dependencies, const std::wstring& filename)
			{
				return html::create_producer(dependencies, { filename });
			},
			false
	);

	auto cef_version_major =	boost::lexical_cast<std::wstring>(cef_version_info(0));
	auto cef_revision =			boost::lexical_cast<std::wstring>(cef_version_info(1));
	auto chrome_major =			boost::lexical_cast<std::wstring>(cef_version_info(2));
	auto chrome_minor =			boost::lexical_cast<std::wstring>(cef_version_info(3));
	auto chrome_build =			boost::lexical_cast<std::wstring>(cef_version_info(4));
	auto chrome_patch =			boost::lexical_cast<std::wstring>(cef_version_info(5));

	dependencies.system_info_provider_repo->register_version_provider(L"cef", [=]
	{
		return cef_version_major + L"." + chrome_build + L"." + cef_revision;
	});
	dependencies.system_info_provider_repo->register_system_info_provider([=](boost::property_tree::wptree& info)
	{
		info.add(L"system.cef.version",			cef_version_major	+ L"." + chrome_build + L"." + cef_revision);
		info.add(L"system.cef.chromeversion",	chrome_major		+ L"." + chrome_minor + L"." + chrome_build + L"." + chrome_patch);
	});
}

void uninit()
{
	invoke([]
	{
		CefQuitMessageLoop();
	});
	g_cef_executor->begin_invoke([&]
	{
		CefShutdown();
	});
	g_cef_executor.reset();
}

class cef_task : public CefTask
{
private:
	std::promise<void> promise_;
	std::function<void ()> function_;
public:
	cef_task(const std::function<void ()>& function)
		: function_(function)
	{
	}

	void Execute() override
	{
		CASPAR_LOG_CALL(trace) << "[cef_task] executing task";

		try
		{
			function_();
			promise_.set_value();
			CASPAR_LOG_CALL(trace) << "[cef_task] task succeeded";
		}
		catch (...)
		{
			promise_.set_exception(std::current_exception());
			CASPAR_LOG(warning) << "[cef_task] task failed";
		}
	}

	std::future<void> future()
	{
		return promise_.get_future();
	}

	IMPLEMENT_REFCOUNTING(cef_task);
};

void invoke(const std::function<void()>& func)
{
	begin_invoke(func).get();
}

std::future<void> begin_invoke(const std::function<void()>& func)
{
	CefRefPtr<cef_task> task = new cef_task(func);

	if (CefCurrentlyOn(TID_UI))
	{
		// Avoid deadlock.
		task->Execute();
		return task->future();
	}

	if (CefPostTask(TID_UI, task.get()))
		return task->future();
	else
		CASPAR_THROW_EXCEPTION(caspar_exception()
				<< msg_info("[cef_executor] Could not post task"));
}

}}
