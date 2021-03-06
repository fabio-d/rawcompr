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

#include "encoders.h"

#include "log.h"

Encoder::Encoder(const AVStream *inputStream, AVFormatContext *outputFormatContext, PacketReferences *outRefs)
: m_inputStream(inputStream), m_outputFormatContext(outputFormatContext), m_outRefs(outRefs), m_outPacketIndex(0)
{
	m_outputStream = avformat_new_stream(outputFormatContext, nullptr);
	if (m_outputStream == nullptr)
		logError("avformat_new_stream failed\n");
}

Encoder::~Encoder()
{
}

void Encoder::finalizeAndWritePacket(const AVPacket *inputPacket, AVPacket *outputPacket)
{
	outputPacket->pts = inputPacket->pts;
	outputPacket->dts = inputPacket->dts;
	outputPacket->duration = inputPacket->duration;
	av_packet_rescale_ts(outputPacket, m_inputStream->time_base, m_outputStream->time_base);

	outputPacket->stream_index = m_outputStream->index;

	logDebug(" -> Output packet: Stream #0:%d (index %zu size %u) - pts %" PRIi64 " dts %" PRIi64 " duration %" PRIi64 "\n",
		outputPacket->stream_index, m_outPacketIndex, outputPacket->size, outputPacket->pts, outputPacket->dts, outputPacket->duration);

	m_outRefs->addPacketReference(m_outputStream->index, m_outPacketIndex, outputPacket->pts, inputPacket->pos, inputPacket->size);
	failOnAVERROR(av_interleaved_write_frame(m_outputFormatContext, outputPacket), "av_write_frame");

	m_outPacketIndex++;
}

VideoEncoder::VideoEncoder(const AVStream *inputStream, AVFormatContext *outputFormatContext, PacketReferences *outRefs, AVCodecID outputCodecID, AVDictionary **outputOptions)
: Encoder(inputStream, outputFormatContext, outRefs),
  m_inputFrame(av_frame_alloc()), m_outputFrame(av_frame_alloc()),
  m_outputPacket(av_packet_alloc())
{
	if (m_inputFrame == nullptr || m_outputFrame == nullptr)
		logError("av_frame_alloc failed\n");

	if (m_outputPacket == nullptr)
		logError("av_packet_alloc failed\n");

	failOnAVERROR(avcodec_parameters_copy(m_outputStream->codecpar, inputStream->codecpar), "avcodec_parameters_copy");
	m_outputStream->codecpar->codec_id = outputCodecID;
        m_outputStream->codecpar->codec_tag = 0;
	m_outputStream->avg_frame_rate = inputStream->avg_frame_rate;
	m_outputStream->time_base = inputStream->time_base;
	m_outputStream->duration = inputStream->duration;

	// Setup decoder

	AVCodec *inputCodec = avcodec_find_decoder(inputStream->codecpar->codec_id);

	m_inputCodecContext = avcodec_alloc_context3(inputCodec);
	if (m_inputCodecContext == nullptr)
		logError("avcodec_alloc_context3 failed\n");

	failOnAVERROR(avcodec_parameters_to_context(m_inputCodecContext, inputStream->codecpar), "avcodec_parameters_to_context");
	failOnAVERROR(avcodec_open2(m_inputCodecContext, inputCodec, nullptr), "avcodec_open2");
	outRefs->addVideoStream(m_inputCodecContext->pix_fmt);

	// Setup encoder

	AVCodec *outputCodec = avcodec_find_encoder(m_outputStream->codecpar->codec_id);

	m_outputCodecContext = avcodec_alloc_context3(outputCodec);
	if (m_outputCodecContext == nullptr)
		logError("avcodec_alloc_context3 failed\n");

	failOnAVERROR(avcodec_parameters_to_context(m_outputCodecContext, m_outputStream->codecpar), "avcodec_parameters_to_context");
	m_outputCodecContext->time_base = inputStream->time_base;
	m_outputCodecContext->pix_fmt = selectCompatibleLosslessPixelFormat(m_inputCodecContext->pix_fmt, outputCodec->pix_fmts);
	m_outputCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	failOnAVERROR(avcodec_open2(m_outputCodecContext, outputCodec, outputOptions), "avcodec_open2");
	failOnAVERROR(avcodec_parameters_from_context(m_outputStream->codecpar, m_outputCodecContext), "avcodec_parameters_from_context");

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
	m_outputFrame->interlaced_frame = m_outputCodecContext->field_order != AV_FIELD_PROGRESSIVE;
	m_outputFrame->top_field_first = m_outputCodecContext->field_order == AV_FIELD_TT || m_outputCodecContext->field_order == AV_FIELD_TB;
	failOnAVERROR(av_frame_get_buffer(m_outputFrame, 0), "av_frame_get_buffer");
}

VideoEncoder::~VideoEncoder()
{
	sws_freeContext(m_swscaleContext);

	av_frame_free(&m_inputFrame);
	av_frame_free(&m_outputFrame);
	av_packet_free(&m_outputPacket);

	avcodec_free_context(&m_inputCodecContext);
	avcodec_free_context(&m_outputCodecContext);
}

void VideoEncoder::processPacket(const AVPacket *inputPacket)
{
	failOnAVERROR(avcodec_send_packet(m_inputCodecContext, inputPacket), "avcodec_send_packet");
	failOnAVERROR(avcodec_receive_frame(m_inputCodecContext, m_inputFrame), "avcodec_receive_frame");

	logDebug(" -> Decoded %dx%d %s pts %" PRIi64 "%s\n", m_inputFrame->width, m_inputFrame->height,
		av_get_pix_fmt_name((AVPixelFormat)m_inputFrame->format), m_inputFrame->pts, m_inputFrame->key_frame ? " KEYFRAME" : "");

	logDebug(" -> Converting from %s to %s\n",
		av_get_pix_fmt_name((AVPixelFormat)m_inputFrame->format),
		av_get_pix_fmt_name((AVPixelFormat)m_outputFrame->format));

	av_frame_make_writable(m_outputFrame);
	sws_scale(m_swscaleContext,
		m_inputFrame->data, m_inputFrame->linesize,
		0, m_inputFrame->height,
		m_outputFrame->data, m_outputFrame->linesize);
	m_outputFrame->pts = m_inputFrame->pts;
	m_outputFrame->key_frame = false;

	failOnAVERROR(avcodec_send_frame(m_outputCodecContext, m_outputFrame), "avcodec_send_frame");
	failOnAVERROR(avcodec_receive_packet(m_outputCodecContext, m_outputPacket), "avcodec_receive_packet");

	logDebug(" -> Encoded %dx%d %s pts %" PRIi64 "%s\n", m_outputFrame->width, m_outputFrame->height,
		av_get_pix_fmt_name((AVPixelFormat)m_outputFrame->format), m_outputFrame->pts,
		(m_outputPacket->flags & AV_PKT_FLAG_KEY) ? " KEYFRAME" : "");

	finalizeAndWritePacket(inputPacket, m_outputPacket);

	av_frame_unref(m_inputFrame);
}

CopyEncoder::CopyEncoder(const AVStream *inputStream, AVFormatContext *outputFormatContext, PacketReferences *outRefs)
: Encoder(inputStream, outputFormatContext, outRefs), m_outputPacket(av_packet_alloc())
{
	if (m_outputPacket == nullptr)
		logError("av_packet_alloc failed\n");

	outRefs->addCopyStream();

	failOnAVERROR(avcodec_parameters_copy(m_outputStream->codecpar, inputStream->codecpar), "avcodec_parameters_copy");
        m_outputStream->codecpar->codec_tag = 0;
}

CopyEncoder::~CopyEncoder()
{
	av_packet_free(&m_outputPacket);
}

void CopyEncoder::processPacket(const AVPacket *inputPacket)
{
	av_packet_ref(m_outputPacket, inputPacket);
	finalizeAndWritePacket(inputPacket, m_outputPacket);
}
