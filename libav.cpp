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

#include <err.h>

static constexpr bool TRACE_SUCCESS = false;

void failOnAVERROR(int errnum, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	char *fmtbuf;
	if (vasprintf(&fmtbuf, fmt, ap) == -1)
		errx(EXIT_FAILURE, "vasprintf failed");

	char errbuf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(errnum, errbuf, sizeof(errbuf));

	if (errnum != 0)
		errx(EXIT_FAILURE, "%s: %s", fmtbuf, errbuf);
	else if (TRACE_SUCCESS)
		warnx("%s: %s", fmtbuf, errbuf);

	free(fmtbuf);

	va_end(ap);
}

static void printLosses(FILE *dest, int losses, const char *suffix)
{
	if (losses == 0)
	{
		fprintf(dest, " LOSSLESS%s", suffix);
	}
	else
	{
		if (losses & FF_LOSS_RESOLUTION)
			fprintf(stderr, " LOSS_RESOLUTION%s", suffix);
		if (losses & FF_LOSS_DEPTH)
			fprintf(stderr, " LOSS_DEPTH%s", suffix);
		if (losses & FF_LOSS_COLORSPACE)
			fprintf(stderr, " LOSS_COLORSPACE%s", suffix);
		if (losses & FF_LOSS_ALPHA)
			fprintf(stderr, " LOSS_ALPHA%s", suffix);
		if (losses & FF_LOSS_COLORQUANT)
			fprintf(stderr, " LOSS_COLORQUANT%s", suffix);
		if (losses & FF_LOSS_CHROMA)
			fprintf(stderr, " LOSS_CHROMA%s", suffix);
	}
}

AVPixelFormat selectCompatibleLosslessPixelFormat(AVPixelFormat src, const enum AVPixelFormat *candidates)
{
	fprintf(stderr, "   -> Input pixel format: %s\n", av_get_pix_fmt_name(src));

	AVPixelFormat result = AV_PIX_FMT_NONE;

	for (; candidates && *candidates != AV_PIX_FMT_NONE; candidates++)
	{
		fprintf(stderr, "   -> Candidate output pixel format: %s", av_get_pix_fmt_name(*candidates));

		int losses = av_get_pix_fmt_loss(*candidates, src, false);
		int lossesInv = av_get_pix_fmt_loss(src, *candidates, true);

		if (losses == 0 && lossesInv == 0)
		{
			fprintf(stderr, "*");
			result = *candidates;
		}

		printLosses(stderr, losses, "");
		printLosses(stderr, lossesInv, "_INV");
		fprintf(stderr, "\n");
	}

	if (result == AV_PIX_FMT_NONE)
		errx(EXIT_FAILURE, "selectCompatibleLosslessPixelFormat: failed to select output pixel format");
	else
		return result;
}
