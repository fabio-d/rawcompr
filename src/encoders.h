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

#ifndef ENCODERS_H
#define ENCODERS_H

#include "llrfile.h"

class Encoder
{
	public:
		Encoder(const AVStream *inputStream, AVFormatContext *outputFormatContext, PacketReferences *outRefs);
		virtual ~Encoder();

		virtual void processPacket(const AVPacket *inputPacket) = 0;

	protected:
		void finalizeAndWritePacket(const AVPacket *inputPacket, AVPacket *outputPacket);

		const AVStream *m_inputStream;

		AVFormatContext *m_outputFormatContext;
		AVStream *m_outputStream;

	private:
		PacketReferences *m_outRefs;
		size_t m_outPacketIndex;
};

class VideoEncoder : public Encoder
{
	public:
		VideoEncoder(const AVStream *inputStream, AVFormatContext *outputFormatContext, PacketReferences *outRefs, AVCodecID outputCodecID, AVDictionary **outputOptions);
		~VideoEncoder() override;

		void processPacket(const AVPacket *inputPacket) override;

	private:
		AVCodecContext *m_inputCodecContext, *m_outputCodecContext;
		AVFrame *m_inputFrame, *m_outputFrame;
		AVPacket *m_outputPacket;

		SwsContext *m_swscaleContext;
};

class CopyEncoder : public Encoder
{
	public:
		CopyEncoder(const AVStream *inputStream, AVFormatContext *outputFormatContext, PacketReferences *outRefs);
		~CopyEncoder() override;

		void processPacket(const AVPacket *inputPacket) override;

	private:
		AVPacket *m_outputPacket;
};

#endif
