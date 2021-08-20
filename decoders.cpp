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

#include "decoders.h"

#include "log.h"

Decoder::Decoder()
{
}

Decoder::~Decoder()
{
}

VideoDecoder::VideoDecoder(const AVStream *inputStream, AVPixelFormat outputPixelFormat)
: m_inputFrame(av_frame_alloc()), m_outputFrame(av_frame_alloc()),
  m_outputPacket(av_packet_alloc())
{
	if (m_inputFrame == nullptr || m_outputFrame == nullptr)
		logError("av_frame_alloc failed\n");

	if (m_outputPacket == nullptr)
		logError("av_packet_alloc failed\n");

	// Setup decoder

	AVCodec *inputCodec = avcodec_find_decoder(inputStream->codecpar->codec_id);

	m_inputCodecContext = avcodec_alloc_context3(inputCodec);
	if (m_inputCodecContext == nullptr)
		logError("avcodec_alloc_context3 failed\n");

	failOnAVERROR(avcodec_parameters_to_context(m_inputCodecContext, inputStream->codecpar), "avcodec_parameters_to_context");
	failOnAVERROR(avcodec_open2(m_inputCodecContext, inputCodec, nullptr), "avcodec_open2");

	// Setup encoder

	AVCodec *outputCodec = avcodec_find_encoder(AV_CODEC_ID_RAWVIDEO);

	m_outputCodecContext = avcodec_alloc_context3(outputCodec);
	if (m_outputCodecContext == nullptr)
		logError("avcodec_alloc_context3 failed\n");

	failOnAVERROR(avcodec_parameters_to_context(m_outputCodecContext, inputStream->codecpar), "avcodec_parameters_to_context");
	m_outputCodecContext->codec_id = AV_CODEC_ID_RAWVIDEO;
        m_outputCodecContext->codec_tag = 0;
	m_outputCodecContext->time_base = inputStream->time_base;
	m_outputCodecContext->pix_fmt = outputPixelFormat;

	failOnAVERROR(avcodec_open2(m_outputCodecContext, outputCodec, nullptr), "avcodec_open2");

	// Setup pixel format converter

	m_swscaleContext = sws_getContext(
		m_inputCodecContext->width, m_inputCodecContext->height, m_inputCodecContext->pix_fmt,
		m_outputCodecContext->width, m_outputCodecContext->height, m_outputCodecContext->pix_fmt,
		0, nullptr, nullptr, nullptr);

	// Setup temporary frame

	m_outputFrame->width = m_outputCodecContext->width;
	m_outputFrame->height = m_outputCodecContext->height;
	m_outputFrame->sample_aspect_ratio = m_outputCodecContext->sample_aspect_ratio;
	m_outputFrame->format = m_outputCodecContext->pix_fmt;
	m_outputFrame->pict_type = AV_PICTURE_TYPE_NONE;
	failOnAVERROR(av_frame_get_buffer(m_outputFrame, 0), "av_frame_get_buffer");
}

VideoDecoder::~VideoDecoder()
{
	sws_freeContext(m_swscaleContext);

	av_frame_free(&m_inputFrame);

	avcodec_free_context(&m_inputCodecContext);
	avcodec_free_context(&m_outputCodecContext);
}

std::vector<uint8_t> VideoDecoder::decodePacket(const AVPacket *inputPacket)
{
	failOnAVERROR(avcodec_send_packet(m_inputCodecContext, inputPacket), "avcodec_send_packet");
	failOnAVERROR(avcodec_receive_frame(m_inputCodecContext, m_inputFrame), "avcodec_receive_frame");

	logDebug(" -> Decoded %dx%d %s pts %" PRIi64 "%s\n", m_inputFrame->width, m_inputFrame->height,
		av_get_pix_fmt_name((AVPixelFormat)m_inputFrame->format), m_inputFrame->pts, m_inputFrame->key_frame ? " KEYFRAME" : "");

	logDebug(" -> Converting from %s to %s\n",
		av_get_pix_fmt_name((AVPixelFormat)m_inputFrame->format),
		av_get_pix_fmt_name((AVPixelFormat)m_outputFrame->format));

	sws_scale(m_swscaleContext,
		m_inputFrame->data, m_inputFrame->linesize,
		0, m_inputFrame->height,
		m_outputFrame->data, m_outputFrame->linesize);

	failOnAVERROR(avcodec_send_frame(m_outputCodecContext, m_outputFrame), "avcodec_send_frame");
	failOnAVERROR(avcodec_receive_packet(m_outputCodecContext, m_outputPacket), "avcodec_receive_packet");

	std::vector<uint8_t> result(m_outputPacket->data, m_outputPacket->data + m_outputPacket->size);

	av_frame_unref(m_inputFrame);
	av_packet_unref(m_outputPacket);

	return result;
}

CopyDecoder::CopyDecoder()
{
}

std::vector<uint8_t> CopyDecoder::decodePacket(const AVPacket *inputPacket)
{
	return std::vector<uint8_t>(inputPacket->data, inputPacket->data + inputPacket->size);
}
