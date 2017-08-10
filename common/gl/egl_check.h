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

#pragma once

#include "../except.h"

namespace caspar { namespace gl {

struct egl_exception                                                    : virtual caspar_exception{};
struct egl_not_initialized                                                 : virtual egl_exception{};
struct egl_bad_access                                                : virtual egl_exception{};
struct egl_bad_alloc                                                : virtual egl_exception{};
struct egl_bad_attriute                                                : virtual egl_exception{};
struct egl_bad_context                                                : virtual egl_exception{};
struct egl_bad_config                                                : virtual egl_exception{};
struct egl_bad_current_surface                                                : virtual egl_exception{};
struct egl_bad_display                                                : virtual egl_exception{};
struct egl_bad_surface                                                : virtual egl_exception{};
struct egl_bad_match                                                : virtual egl_exception{};
struct egl_bad_parameter                                                : virtual egl_exception{};
struct egl_bad_native_pixmap                                                : virtual egl_exception{};
struct egl_bad_native_window                                                : virtual egl_exception{};
struct egl_context_lost                                                : virtual egl_exception{};

void SMFL_EGLCheckError(const std::string& expr, const char* func, const char* file, unsigned int line);

//#ifdef _DEBUG
#define CASPAR_EGL_EXPR_STR(expr) #expr

#define eglCheck(expr) expr; caspar::gl::SMFL_EGLCheckError(CASPAR_EGL_EXPR_STR(expr), __FUNCTION__, __FILE__, __LINE__);

}}
