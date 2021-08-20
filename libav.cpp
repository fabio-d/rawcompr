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

#include "libav.h"

#include "log.h"

static constexpr bool TRACE_SUCCESS = false;

void failOnAVERROR(int errnum, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	char *fmtbuf;
	if (vasprintf(&fmtbuf, fmt, ap) == -1)
		logError("vasprintf failed\n");

	char errbuf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(errnum, errbuf, sizeof(errbuf));

	if (errnum != 0)
		logError("%s: %s\n", fmtbuf, errbuf);
	else if (TRACE_SUCCESS)
		logWarning("%s: %s\n", fmtbuf, errbuf);

	free(fmtbuf);

	va_end(ap);
}

void seekOrFail(AVIOContext *s, int64_t offset)
{
	int64_t r = avio_seek(s, offset, SEEK_SET);
	if (r != offset)
		logError("avio_seek failed");
}

void _failOnWriteError(AVIOContext *s, const char *op)
{
	failOnAVERROR(s->error, "%s", op);
}

void writeInChunks(AVIOContext *s, const unsigned char *buf, int size)
{
	while (size != 0)
	{
		int chunkSize = std::min(size, s->max_packet_size);
		failOnWriteError(avio_write, s, buf, chunkSize);

		buf += chunkSize;
		size -= chunkSize;
	}
}

AVPixelFormat selectCompatibleLosslessPixelFormat(AVPixelFormat src, const enum AVPixelFormat *candidates)
{
	auto printLosses = [](int losses, const char *suffix)
	{
		if (losses == 0)
		{
			logDebug(" LOSSLESS%s", suffix);
		}
		else
		{
			if (losses & FF_LOSS_RESOLUTION)
				logDebug(" LOSS_RESOLUTION%s", suffix);
			if (losses & FF_LOSS_DEPTH)
				logDebug(" LOSS_DEPTH%s", suffix);
			if (losses & FF_LOSS_COLORSPACE)
				logDebug(" LOSS_COLORSPACE%s", suffix);
			if (losses & FF_LOSS_ALPHA)
				logDebug(" LOSS_ALPHA%s", suffix);
			if (losses & FF_LOSS_COLORQUANT)
				logDebug(" LOSS_COLORQUANT%s", suffix);
			if (losses & FF_LOSS_CHROMA)
				logDebug(" LOSS_CHROMA%s", suffix);
		}
	};

	logDebug("   -> Input pixel format: %s\n", av_get_pix_fmt_name(src));

	AVPixelFormat result = AV_PIX_FMT_NONE;

	for (; candidates && *candidates != AV_PIX_FMT_NONE; candidates++)
	{
		logDebug("   -> Candidate output pixel format: %s", av_get_pix_fmt_name(*candidates));

		int losses = av_get_pix_fmt_loss(*candidates, src, false);
		int lossesInv = av_get_pix_fmt_loss(src, *candidates, true);

		if (losses == 0 && lossesInv == 0)
		{
			logDebug("*");
			result = *candidates;
		}

		printLosses(losses, "");
		printLosses(lossesInv, "_INV");
		logDebug("\n");
	}

	if (result == AV_PIX_FMT_NONE)
		logError("selectCompatibleLosslessPixelFormat: failed to select output pixel format\n");
	else
		return result;
}

std::vector<std::string> enumerateHashAlgorithms()
{
	std::vector<std::string> result;

	int i = 0;
	while (true)
	{
		const char *hashName = av_hash_names(i++);
		if (hashName == nullptr)
			break;

		result.push_back(hashName);
	}

	return result;
}
