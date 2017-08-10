///////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2009 Laurent Gomila (laurent.gom@gmail.com)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
///////////////////////////

#include "../stdafx.h"

#include "egl_check.h"

#include "../log.h"

#include <EGL/egl.h>

namespace caspar { namespace gl {

void SMFL_EGLCheckError(const std::string& expr, const char* func, const char* file, unsigned int line)
{
    // Obtain information about the success or failure of the most recent EGL
    // function called in the current thread
    EGLint errorCode = eglGetError();

    if (errorCode != EGL_SUCCESS)
    {
        // Decode the error code returned
        switch (errorCode)
        {
            case EGL_NOT_INITIALIZED:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_not_initialized()
                                                << msg_info("EGL is not initialized, or could not be initialized, for the specified display")
                                                << error_info("EGL_NOT_INITIALIZED")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_ACCESS:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_access()
                                                << msg_info("EGL cannot access a requested resource (for example, a context is bound in another thread)")
                                                << error_info("EGL_BAD_ACCESS")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_ALLOC:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_alloc()
                                                << msg_info("EGL failed to allocate resources for the requested operation")
                                                << error_info("EGL_BAD_ALLOC")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }
            case EGL_BAD_ATTRIBUTE:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_attriute()
                                                << msg_info("an unrecognized attribute or attribute value was passed in an attribute list")
                                                << error_info("EGL_BAD_ATTRIBUTE")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_CONTEXT:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_context()
                                                << msg_info("an EGLContext argument does not name a valid EGLContext")
                                                << error_info("EGL_BAD_CONTEXT")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_CONFIG:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_config()
                                                << msg_info("an EGLConfig argument does not name a valid EGLConfig")
                                                << error_info("EGL_BAD_CONFIG")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_CURRENT_SURFACE:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_current_surface()
                                                << msg_info("the current surface of the calling thread is a window, pbuffer, or pixmap that is no longer valid")
                                                << error_info("EGL_BAD_CURRENT_SURFACE")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_DISPLAY:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_display()
                                                << msg_info("an EGLDisplay argument does not name a valid EGLDisplay; or, EGL is not initialized on the specified EGLDisplay")
                                                << error_info("EGL_BAD_DISPLAY")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_SURFACE:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_surface()
                                                << msg_info("an EGLSurface argument does not name a valid surface (window, pbuffer, or pixmap) configured for rendering")
                                                << error_info("EGL_BAD_SURFACE")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_MATCH:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_match()
                                                << msg_info("arguments are inconsistent; for example, an otherwise valid context requires buffers (e.g. depth or stencil) not allocated by an otherwise valid surface")
                                                << error_info("EGL_BAD_MATCH")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_PARAMETER:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_parameter()
                                                << msg_info("one or more argument values are invalid")
                                                << error_info("EGL_BAD_PARAMETER")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_NATIVE_PIXMAP:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_native_pixmap()
                                                << msg_info("an EGLNativePixmapType argument does not refer to a valid native pixmap")
                                                << error_info("EGL_BAD_NATIVE_PIXMAP")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_BAD_NATIVE_WINDOW:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_bad_native_window()
                                                << msg_info("an EGLNativeWindowType argument does not refer to a valid native window")
                                                << error_info("EGL_BAD_NATIVE_WINDOW")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }

            case EGL_CONTEXT_LOST:
            {
                ::boost::exception_detail::throw_exception_(
                                        egl_context_lost()
                                                << msg_info("a power management event has occurred. The application must destroy all contexts and reinitialize client API state and objects to continue rendering")
                                                << error_info("EGL_CONTEXT_LOST")
                                                << call_stack_info(caspar::get_call_stack())
                                                << context_info(get_context()),
                                        func, log::remove_source_prefix(file), line);
            }
        }
    }
}

}}
