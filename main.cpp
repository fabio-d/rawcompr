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
#include "decoders.h"
#include "encoders.h"
#include "log.h"

#include <map>
#include <stdio.h>
#include <stdlib.h>

static int compress(const CommandLine &cmd)
{
	AVFormatContext *inputFormatContext = nullptr, *outputFormatContext = nullptr;

	const char *inputFilename = cmd.inputFile();
	const char *outputFilename = cmd.outputFile();
	const char *llrFilename = cmd.llrFile();

	failOnAVERROR(avformat_open_input(&inputFormatContext, inputFilename, nullptr, nullptr), "avformat_open_input: %s", inputFilename);
	failOnAVERROR(avformat_find_stream_info(inputFormatContext, nullptr), "avformat_find_stream_info");
	av_dump_format(inputFormatContext, 0, inputFilename, false);

	logDebug("Encoders:\n");
	failOnAVERROR(avformat_alloc_output_context2(&outputFormatContext, nullptr, "matroska", outputFilename), "avformat_alloc_output_context2: %s", outputFilename);

	std::map<int, Encoder*> encoders;
	PacketReferences packetRefs;

	for (unsigned int i = 0; i < inputFormatContext->nb_streams; i++)
	{
		const AVStream *inputStream = inputFormatContext->streams[i];
		AVCodecParameters *inputCodecParameters = inputStream->codecpar;

		const char *codecName = avcodec_get_name(inputCodecParameters->codec_id); // never nullptr (according to documentation)
		logDebug("  Stream #0:%d: input_codec=%s output_codec=", inputStream->index, codecName);

		Encoder *encoder = nullptr;
		if (strcmp(codecName, "rawvideo") == 0)
		{
			logDebug("%s\n", avcodec_get_name(cmd.videoCodec()));

			AVDictionary *opts = nullptr;
			cmd.fillVideoCodecOptions(&opts);
			encoder = new VideoEncoder(inputStream, outputFormatContext, &packetRefs, cmd.videoCodec(), &opts);
			av_dict_free(&opts);
		}

		if (encoder == nullptr)
		{
			logDebug("copy\n");
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

		logDebug("Input packet: Stream #0:%d (pos %" PRIi64 " size %u) - pts %" PRIi64 " dts %" PRIi64 " duration %" PRIi64 "\n",
			packet->stream_index, packet->pos, packet->size, packet->pts, packet->dts, packet->duration);

		Encoder *encoder = encoders.at(packet->stream_index);
		encoder->processPacket(packet);

		av_packet_unref(packet);
	}

	av_packet_free(&packet);

	writeLLR(inputFormatContext->pb, &packetRefs, llrFile, cmd.hashName().c_str());

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

static bool verifyHash(AVIOContext *file, int64_t fileSize, const char *hashName, const std::vector<uint8_t> &expectedHash)
{
	static unsigned char buffer[4096];

	AVHashContext *hashCtx;
	int r = av_hash_alloc(&hashCtx, hashName);
	if (r == AVERROR(EINVAL))
		logError("Hash verification failed: algorithm \"%s\" is not supported (is libavutil up to date?)\n", hashName);

	failOnAVERROR(r, "av_hash_alloc");
	av_hash_init(hashCtx);

	int hashSize = av_hash_get_size(hashCtx);
	if (hashSize != expectedHash.size())
		logError("Hash verification failed: hash size mismatch\n");

	int64_t pos = 0;
	seekOrFail(file, 0);

	logDebug("Computing final hash:\n");
	while (pos != fileSize)
	{
		int r = avio_read(file, buffer, std::min<int64_t>(fileSize - pos, sizeof(buffer)));
		if (r == 0)
			logError("avio_read_partial: Premature end of file\n");
		else if (r < 0)
			failOnAVERROR(r, "avio_read_partial");

		logDebug("   -> %" PRIi64 "-%" PRIi64 ": size %d\n", pos, pos + r, r);
		av_hash_update(hashCtx, buffer, r);

		pos += r;
	}

	std::vector<uint8_t> hashBuffer(hashSize);
	av_hash_final(hashCtx, hashBuffer.data());
	av_hash_freep(&hashCtx);

	logDebug("Final %s hash is ", hashName);
	for (int i = 0; i < hashSize; i++)
		logDebug("%02x", hashBuffer[i]);
	logDebug("\n");

	if (hashBuffer == expectedHash)
		return true;

	logError("Hash verification failed: corrupt file\n");
	return false;
}

static int decompress(const CommandLine &cmd)
{
	AVFormatContext *inputFormatContext = nullptr;

	const char *inputFilename = cmd.inputFile();
	const char *outputFilename = cmd.outputFile();
	const char *llrFilename = cmd.llrFile();

	failOnAVERROR(avformat_open_input(&inputFormatContext, inputFilename, nullptr, nullptr), "avformat_open_input: %s", inputFilename);
	failOnAVERROR(avformat_find_stream_info(inputFormatContext, nullptr), "avformat_find_stream_info");
	av_dump_format(inputFormatContext, 0, inputFilename, false);

	AVIOContext *llrFile, *outputFile;
	failOnAVERROR(avio_open(&llrFile, llrFilename, AVIO_FLAG_READ), "avio_open: %s", llrFilename);
	failOnAVERROR(avio_open(&outputFile, outputFilename, AVIO_FLAG_READ_WRITE | AVIO_FLAG_DIRECT), "avio_open: %s", outputFilename);

	std::map<int, Decoder*> decoders;
	PacketReferences packetRefs;

	const LLRInfo info = readLLR(llrFile, &packetRefs, outputFile);
	if (packetRefs.streams().size() != inputFormatContext->nb_streams)
		logError("Stream count mismatch\n");

	logDebug("Decoders:\n");
	for (unsigned int i = 0; i < inputFormatContext->nb_streams; i++)
	{
		const PacketReferences::StreamInfo &info = packetRefs.streams().at(i);

		const AVStream *inputStream = inputFormatContext->streams[i];
		AVCodecParameters *inputCodecParameters = inputStream->codecpar;

		const char *codecName = avcodec_get_name(inputCodecParameters->codec_id); // never nullptr (according to documentation)
		logDebug("  Stream #0:%d: input_codec=%s output_codec=", inputStream->index, codecName);

		Decoder *decoder;
		switch (info.type)
		{
			case Video:
			{
				logDebug("rawvideo %s\n", info.pixelFormat.c_str());

				AVPixelFormat outputPixelFormat = av_get_pix_fmt(info.pixelFormat.c_str());
				if (outputPixelFormat == AV_PIX_FMT_NONE)
					logError("Invalid pixel format string\n");

				decoder = new VideoDecoder(inputStream, outputPixelFormat);
				break;
			}
			case Copy:
			{
				logDebug("copy\n");
				decoder = new CopyDecoder();
				break;
			}
			default:
				abort();
		}

		decoders.emplace(inputStream->index, decoder);
	}

	// Build reverse packet mapping (streamIndex, packetIndex, pts) -> (origPos, origSize)
	std::map<std::tuple<int, size_t, int64_t>, std::pair<int64_t, int>> reverseRefs;
	for (const auto &[origPos, e] : packetRefs.table())
		reverseRefs.insert({{e.streamIndex, e.packetIndex, e.pts}, {origPos, e.origSize}});

	// Decode (uncompress) packets
	std::map<int, size_t> packetIndexPerStream;
	AVPacket *packet = av_packet_alloc();
	while (true)
	{
		int errnum = av_read_frame(inputFormatContext, packet);
		if (errnum == AVERROR_EOF)
			break;
		else
			failOnAVERROR(errnum, "av_read_frame");

		size_t packetIndex = packetIndexPerStream[packet->stream_index]++;
		logDebug("Input packet: Stream #0:%d (index %zu) - pts %" PRIi64 " dts %" PRIi64 " duration %" PRIi64 "\n",
			packet->stream_index, packetIndex, packet->pts, packet->dts, packet->duration);

		auto it = reverseRefs.find({packet->stream_index, packetIndex, packet->pts});
		if (it == reverseRefs.end())
			logError("Failed to find destination block\n");

		Decoder *decoder = decoders.at(packet->stream_index);
		std::vector<uint8_t> uncompressedData = decoder->decodePacket(packet);
		if (uncompressedData.size() != it->second.second) // check origSize
			logError("Decoded to %zu bytes (actual) instead of %d bytes (expected)\n", uncompressedData.size(), it->second.second);

		int64_t start = it->second.first; // origPos
		logDebug(" -> %" PRIi64 "-%" PRIi64 ": writing %" PRIi64 " bytes\n", start, start + uncompressedData.size(), uncompressedData.size());

		seekOrFail(outputFile, start);
		writeInChunks(outputFile, uncompressedData.data(), (int)uncompressedData.size());

		reverseRefs.erase(it);
		av_packet_unref(packet);
	}

	av_packet_free(&packet);

	failOnAVERROR(avio_closep(&llrFile), "avio_closep");

	avformat_close_input(&inputFormatContext);

	if (!reverseRefs.empty())
		logError("One or more source packets are missing\n");

	// Verify hash
	bool hashOk = verifyHash(outputFile, info.originalFileSize, info.hashName.c_str(), info.hashBuffer);
	failOnAVERROR(avio_closep(&outputFile), "avio_closep");

	return hashOk ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
	CommandLine cmd(argc, argv);

	setupLogDebug(cmd.enableLogDebug());

	//av_log_set_level(AV_LOG_DEBUG);

	if (cmd.operation() == CommandLine::Compress)
		return compress(cmd);
	else
		return decompress(cmd);
}
