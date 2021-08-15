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

#include "transcoders.h"

#include <err.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	AVFormatContext *inputFormatContext = nullptr, *outputFormatContext = nullptr;
	int ret;

	//av_log_set_level(AV_LOG_DEBUG);

	const char *inputFilename = argv[1];
	const char *outputFilename = "/dev/shm/remuxed_cxx.mkv";

	failOnAVERROR(avformat_open_input(&inputFormatContext, inputFilename, nullptr, nullptr), "avformat_open_input: %s", inputFilename);
	failOnAVERROR(avformat_find_stream_info(inputFormatContext, nullptr), "avformat_find_stream_info");
	av_dump_format(inputFormatContext, 0, inputFilename, false);

	fprintf(stderr, "Transcoders:\n");
	failOnAVERROR(avformat_alloc_output_context2(&outputFormatContext, nullptr, "matroska", outputFilename), "avformat_alloc_output_context2: %s", outputFilename);

	std::map<int, Transcoder*> transcoders;
	for (unsigned int i = 0; i < inputFormatContext->nb_streams; i++)
	{
		const AVStream *inputStream = inputFormatContext->streams[i];
		AVCodecParameters *inputCodecParameters = inputStream->codecpar;

		const char *codecName = avcodec_get_name(inputCodecParameters->codec_id); // never nullptr (according to documentation)
		fprintf(stderr, "  Stream #0:%d: input_codec=%s output_codec=", inputStream->index, codecName);

		Transcoder *transcoder = nullptr;
		if (strcmp(codecName, "rawvideo") == 0)
		{
			fprintf(stderr, "ffv1\n");

			AVDictionary *opts = nullptr;
			failOnAVERROR(av_dict_set(&opts, "level", "3", 0), "av_dict_set");
			failOnAVERROR(av_dict_set(&opts, "slicecrc", "0", 0), "av_dict_set");
			failOnAVERROR(av_dict_set(&opts, "context", "1", 0), "av_dict_set");
			failOnAVERROR(av_dict_set(&opts, "coder", "range_def", 0), "av_dict_set");
			failOnAVERROR(av_dict_set(&opts, "g", "600", 0), "av_dict_set");
			failOnAVERROR(av_dict_set(&opts, "slices", "4", 0), "av_dict_set");
			transcoder = new VideoCompressorTranscoder(inputStream, outputFormatContext, AV_CODEC_ID_FFV1, &opts);
			av_dict_free(&opts);
		}

		if (transcoder == nullptr)
		{
			fprintf(stderr, "copy\n");
			transcoder = new PassthroughTranscoder(inputStream, outputFormatContext);
		}

		transcoders.emplace(inputStream->index, transcoder);
	}

	av_dump_format(outputFormatContext, 0, outputFilename, true);

	if ((outputFormatContext->oformat->flags & AVFMT_NOFILE) == 0)
		failOnAVERROR(avio_open(&outputFormatContext->pb, outputFilename, AVIO_FLAG_WRITE), "avio_open: %s", outputFilename);

	failOnAVERROR(avformat_write_header(outputFormatContext, nullptr), "avformat_write_header");

	AVPacket *packet = av_packet_alloc();
	while (true)
	{
		int errnum = av_read_frame(inputFormatContext, packet);
		if (errnum == AVERROR_EOF)
			break;
		else
			failOnAVERROR(errnum, "av_read_frame");

		fprintf(stderr, "Input packet: Stream #0:%d (pos %" PRIi64 " size %u) - pts %" PRIi64 " dts %" PRIi64 " duration %" PRIi64 "\n",
			packet->stream_index, packet->pos, packet->size, packet->pts, packet->dts, packet->duration);

		Transcoder *transcoder = transcoders.at(packet->stream_index);
		transcoder->processPacket(packet);

		av_packet_unref(packet);
	}

	failOnAVERROR(av_write_trailer(outputFormatContext), "av_write_trailer");

	if ((outputFormatContext->oformat->flags & AVFMT_NOFILE) == 0)
		failOnAVERROR(avio_closep(&outputFormatContext->pb), "avio_closep");

	avformat_close_input(&inputFormatContext);
	avformat_free_context(outputFormatContext);
}
