/* rawcompr: Losslessly compress raw streams in multimedia files.
 * Copyright (C) 2021  Fabio D'Urso <fabiodurso@hotmail.it>
 *
 * "rawcompr" is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBAV_H
#define LIBAV_H

#include <stdint.h>
#include <string>
#include <vector>

extern "C"
{

#include <libavformat/avformat.h>
#include <libavutil/common.h>
#include <libavutil/hash.h>
#include <libavutil/pixdesc.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>

}

void failOnAVERROR(int errnum, const char *fmt, ...)  __attribute__((format(printf, 2, 3)));
void seekOrFail(AVIOContext *s, int64_t offset);

void _failOnWriteError(AVIOContext *s, const char *op);
#define failOnWriteError(op, s, ...) do { _failOnWriteError(s, "precondition"); op(s, __VA_ARGS__); _failOnWriteError(s, #op); } while(false)

// If AVIO_FLAG_DIRECT is set, ffurl_write says "avoid sending too big packets" and fails if we try to write more than max_packet_size
void writeInChunks(AVIOContext *s, const unsigned char *buf, int size);

AVPixelFormat selectCompatibleLosslessPixelFormat(AVPixelFormat src, const enum AVPixelFormat *candidates /* -1 terminator */);
std::vector<std::string> enumerateHashAlgorithms();

#endif
