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

#ifndef TRANSCODERS_H
#define TRANSCODERS_H

#include "libav.h"

class Transcoder
{
	public:
		explicit Transcoder(const AVStream *inputStream, AVFormatContext *outputFormatContext);
		virtual ~Transcoder();

		virtual void processPacket(const AVPacket *inputPacket) = 0;

	protected:
		const AVStream *m_inputStream;

		AVFormatContext *m_outputFormatContext;
		AVStream *m_outputStream;
};

class VideoCompressorTranscoder : public Transcoder
{
	public:
		VideoCompressorTranscoder(const AVStream *inputStream, AVFormatContext *outputFormatContext, AVCodecID outputCodecID, AVDictionary **outputOptions);

		void processPacket(const AVPacket *inputPacket) override;

	private:
		AVCodecContext *m_inputCodecContext, *m_outputCodecContext;
		AVFrame *m_inputFrame, *m_outputFrame;
		AVPacket *m_outputPacket;

		SwsContext *m_swscaleContext;
};

class PassthroughTranscoder : public Transcoder
{
	public:
		PassthroughTranscoder(const AVStream *inputStream, AVFormatContext *outputFormatContext);

		void processPacket(const AVPacket *inputPacket) override;
};

#endif
