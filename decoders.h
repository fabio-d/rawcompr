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

#ifndef DECODERS_H
#define DECODERS_H

#include "llrfile.h"

class Decoder
{
	public:
		Decoder();
		virtual ~Decoder();

		virtual std::vector<uint8_t> decodePacket(const AVPacket *inputPacket) = 0;
};

class VideoDecoder : public Decoder
{
	public:
		VideoDecoder(const AVStream *inputStream, AVPixelFormat outputPixelFormat);
		virtual ~VideoDecoder();

		std::vector<uint8_t> decodePacket(const AVPacket *inputPacket) override;

	private:
		AVCodecContext *m_inputCodecContext, *m_outputCodecContext;
		AVFrame *m_inputFrame, *m_outputFrame;
		AVPacket *m_outputPacket;

		SwsContext *m_swscaleContext;
};

class CopyDecoder : public Decoder
{
	public:
		CopyDecoder();

		std::vector<uint8_t> decodePacket(const AVPacket *inputPacket) override;
};

#endif
