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

#include "commandline.h"
#include "encoders.h"

#include <err.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>

int compress(const CommandLine &cmd)
{
	AVFormatContext *inputFormatContext = nullptr, *outputFormatContext = nullptr;
	int ret;

	const char *inputFilename = cmd.inputFile();
	const char *outputFilename = cmd.outputFile();
	const char *llrFilename = cmd.llrFile();

	failOnAVERROR(avformat_open_input(&inputFormatContext, inputFilename, nullptr, nullptr), "avformat_open_input: %s", inputFilename);
	failOnAVERROR(avformat_find_stream_info(inputFormatContext, nullptr), "avformat_find_stream_info");
	av_dump_format(inputFormatContext, 0, inputFilename, false);

	fprintf(stderr, "Encoders:\n");
	failOnAVERROR(avformat_alloc_output_context2(&outputFormatContext, nullptr, "matroska", outputFilename), "avformat_alloc_output_context2: %s", outputFilename);

	std::map<int, Encoder*> encoders;
	PacketReferences packetRefs;

	for (unsigned int i = 0; i < inputFormatContext->nb_streams; i++)
	{
		const AVStream *inputStream = inputFormatContext->streams[i];
		AVCodecParameters *inputCodecParameters = inputStream->codecpar;

		const char *codecName = avcodec_get_name(inputCodecParameters->codec_id); // never nullptr (according to documentation)
		fprintf(stderr, "  Stream #0:%d: input_codec=%s output_codec=", inputStream->index, codecName);

		Encoder *encoder = nullptr;
		if (strcmp(codecName, "rawvideo") == 0)
		{
			fprintf(stderr, "%s\n", avcodec_get_name(cmd.videoCodec()));

			AVDictionary *opts = nullptr;
			cmd.fillVideoCodecOptions(&opts);
			encoder = new VideoEncoder(inputStream, outputFormatContext, &packetRefs, cmd.videoCodec(), &opts);
			av_dict_free(&opts);
		}

		if (encoder == nullptr)
		{
			fprintf(stderr, "copy\n");
			encoder = new CopyEncoder(inputStream, outputFormatContext, &packetRefs);
		}

		encoders.emplace(inputStream->index, encoder);
	}

	av_dump_format(outputFormatContext, 0, outputFilename, true);

	if ((outputFormatContext->oformat->flags & AVFMT_NOFILE) == 0)
		failOnAVERROR(avio_open(&outputFormatContext->pb, outputFilename, AVIO_FLAG_WRITE), "avio_open: %s", outputFilename);

	AVIOContext *llrFile;
	failOnAVERROR(avio_open(&llrFile, llrFilename, AVIO_FLAG_WRITE), "avio_open: %s", llrFilename);

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

		Encoder *encoder = encoders.at(packet->stream_index);
		encoder->processPacket(packet);

		av_packet_unref(packet);
	}

	av_packet_free(&packet);

	//packetRefs.dump(stderr);
	writeLLR(inputFormatContext->pb, &packetRefs, llrFile);

	failOnAVERROR(av_write_trailer(outputFormatContext), "av_write_trailer");

	if ((outputFormatContext->oformat->flags & AVFMT_NOFILE) == 0)
		failOnAVERROR(avio_closep(&outputFormatContext->pb), "avio_closep");

	failOnAVERROR(avio_closep(&llrFile), "avio_closep");

	for (const auto it : encoders)
		delete it.second;

	avformat_close_input(&inputFormatContext);
	avformat_free_context(outputFormatContext);

	return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	CommandLine cmd(argc, argv);

	//av_log_set_level(AV_LOG_DEBUG);

	if (cmd.operation() == CommandLine::Compress)
		return compress(cmd);
	else
		return EXIT_FAILURE; // not implemented yet
}
