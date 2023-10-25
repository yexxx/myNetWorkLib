/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *  Copyright (c) 2016-2019 xiongziliang <771730766@qq.com>
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "uv_errno.hpp"

#include <cerrno>
#include <cstdio>

namespace myNet {

static const char* uv__unknown_err_code(int err) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "Unknown system error %d", err);
    return buf;
}

#define UV_ERR_NAME_GEN(name, _)                                                                                                                                                                       \
    case UV_##name:                                                                                                                                                                                    \
        return #name;
const char* uv_err_name(int err) {
    switch (err) { UV_ERRNO_MAP(UV_ERR_NAME_GEN) }
    return uv__unknown_err_code(err);
}
#undef UV_ERR_NAME_GEN

#define UV_STRERROR_GEN(name, msg)                                                                                                                                                                     \
    case UV_##name:                                                                                                                                                                                    \
        return msg;
const char* uv_strerror(int err) {
    switch (err) { UV_ERRNO_MAP(UV_STRERROR_GEN) }
    return uv__unknown_err_code(err);
}
#undef UV_STRERROR_GEN

int uv_translate_posix_error(int err) {
    if (err <= 0) {
        return err;
    }
    switch (err) {
    // 为了兼容windows/unix平台，信号EINPROGRESS ，EAGAIN，EWOULDBLOCK，ENOBUFS 全部统一成EAGAIN处理
    case ENOBUFS: // 在mac系统下实测发现会有此信号发生
    case EINPROGRESS:
    case EWOULDBLOCK:
        err = EAGAIN;
        break;
    default:
        break;
    }
    return -err;
}

} // namespace myNet