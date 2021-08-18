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

#include "llrfile.h"

#include <err.h>
#include <inttypes.h>
#include <stdlib.h>

static constexpr int32_t LLR_MAGIC_SIGNATURE = MKBETAG('L', 'L', 'R', '\0');
static constexpr int64_t LLR_BUFFER_SIZE = 4096;

void PacketReferences::addVideoStream(AVPixelFormat pixelFormat)
{
	StreamInfo info;
	info.type = Video;
	info.pixelFormat = av_get_pix_fmt_name(pixelFormat);
	m_streams.push_back(info);
}

void PacketReferences::addCopyStream()
{
	StreamInfo info;
	info.type = Copy;
	m_streams.push_back(info);
}

void PacketReferences::addPacketReference(int streamIndex, size_t packetIndex, int64_t pts, int64_t origPos, int origSize)
{
	ReferenceInfo info;
	info.origSize = origSize;
	info.streamIndex = streamIndex;
	info.packetIndex = packetIndex;
	info.pts = pts;

	auto [it, inserted] = m_table.emplace(origPos, info);
	if (!inserted)
	{
bug_halt:
		errx(EXIT_FAILURE, "addPacketReference: overlapping range, probably a bug. halting!");
	}

	if (++it != m_table.end() && it->first < origPos + origSize)
		goto bug_halt;
}

const std::vector<PacketReferences::StreamInfo> &PacketReferences::streams() const
{
	return m_streams;
}

const std::map<size_t, PacketReferences::ReferenceInfo> &PacketReferences::table() const
{
	return m_table;
}

void PacketReferences::dump(FILE *dest) const
{
	fprintf(dest, "Streams (total %zu):\n", m_streams.size());

	for (size_t i = 0; i < m_streams.size(); i++)
	{
		const StreamInfo &info = m_streams.at(i);
		fprintf(dest, "  Stream #0:%zu: ", i);

		switch (info.type)
		{
			case Video:
				fprintf(dest, "video %s\n", info.pixelFormat.c_str());
				break;
			case Copy:
				fprintf(dest, "copy\n");
				break;
			default:
				abort();
		}
	}

	fprintf(dest, "Packet references (total %zu):\n", m_table.size());

	for (const auto &[origPos, e] : m_table)
	{
		fprintf(dest, "  %" PRIi64 "-%" PRIi64 ": Stream #0:%d (index %zu) - pts %" PRIi64 " size %d\n",
			origPos, origPos + e.origSize, e.streamIndex, e.packetIndex, e.pts, e.origSize);
	}
}

void PacketReferences::deserialize(AVIOContext *src)
{
	m_streams.clear();
	m_table.clear();

	int32_t streamCount = avio_rb32(src);
	while (streamCount-- != 0)
	{
		StreamInfo info;
		info.type = (CodecType)avio_r8(src);

		switch (info.type)
		{
			case Video:
			{
				char buffer[128];
				avio_get_str(src, sizeof(buffer) - 1, buffer, sizeof(buffer));
				info.pixelFormat = buffer;
				break;
			}
			case Copy:
			{
				break;
			}
			default:
			{
				abort(); // this should never happen
			}
		}

		m_streams.push_back(info);
	}


	int64_t tableCount = avio_rb64(src);
	while (tableCount-- != 0)
	{
		int64_t origPos = avio_rb64(src);

		ReferenceInfo info;
		info.origSize = avio_rb32(src);
		info.streamIndex = avio_rb32(src);
		info.packetIndex = avio_rb64(src);
		info.pts = avio_rb64(src);

		m_table.emplace(origPos, info);
	}
}

void PacketReferences::serialize(AVIOContext *dest) const
{
	avio_wb32(dest, m_streams.size());
	for (const StreamInfo &e : m_streams)
	{
		avio_w8(dest, e.type);

		switch (e.type)
		{
			case Video:
			{
				avio_put_str(dest, e.pixelFormat.c_str());
				break;
			}
			case Copy:
			{
				break;
			}
			default:
			{
				abort(); // this should never happen
			}
		}
	}

	avio_wb64(dest, m_table.size());
	for (const auto &[origPos, e] : m_table)
	{
		avio_wb64(dest, origPos);
		avio_wb32(dest, e.origSize);
		avio_wb32(dest, e.streamIndex);
		avio_wb64(dest, e.packetIndex);
		avio_wb64(dest, e.pts);
	}
}

void writeLLR(AVIOContext *inputFile, const PacketReferences *packetRefs, AVIOContext *llrFile)
{
	unsigned char buffer[LLR_BUFFER_SIZE];

	fprintf(stderr, "Writing LLR file:\n");
	avio_wb32(llrFile, LLR_MAGIC_SIGNATURE);

	int64_t inputSize = avio_size(inputFile);
	int64_t prevOffset = 0;

	avio_wb64(llrFile, inputSize);
	packetRefs->serialize(llrFile);

	auto embedChunk = [&](int64_t start, int64_t end)
	{
		fprintf(stderr, "  %" PRIi64 "-%" PRIi64 ": Embedding - size %" PRIi64 "\n", start, end, end - start);
		avio_seek(inputFile, start, SEEK_SET);

		while (start != end)
		{
			int64_t r = avio_read_partial(inputFile, buffer, std::min(LLR_BUFFER_SIZE, end - start));
			if (r == 0)
				errx(EXIT_FAILURE, "avio_read_partial: Premature end of file");
			else if (r < 0)
				failOnAVERROR(r, "avio_read_partial");

			fprintf(stderr, "   -> %" PRIi64 "-%" PRIi64 ": size %" PRIi64 "\n", start, start + r, r);

			avio_write(llrFile, buffer, r);
			start += r;
		}
	};

	for (const auto &[origPos, e] : packetRefs->table())
	{
		if (origPos != prevOffset)
		{
			embedChunk(prevOffset, origPos);
			prevOffset = origPos;
		}

		prevOffset += e.origSize;

		fprintf(stderr, "  %" PRIi64 "-%" PRIi64 ": Referencing stream #0:%d (index %zu) - pts %" PRIi64 " size %d\n",
			origPos, prevOffset, e.streamIndex, e.packetIndex, e.pts, e.origSize);
	}

	if (prevOffset != inputSize)
		embedChunk(prevOffset, inputSize);
}

void readLLR(AVIOContext *llrFile, PacketReferences *outPacketRefs, AVIOContext *outputFile)
{
	unsigned char buffer[LLR_BUFFER_SIZE];

	if (avio_rb32(llrFile) != LLR_MAGIC_SIGNATURE)
		err(EXIT_FAILURE, "Invalid LLR file signature");

	fprintf(stderr, "Reading LLR file:\n");

	int64_t outputSize = avio_rb64(llrFile);
	int64_t prevOffset = 0;

	fprintf(stderr, "  Original file size: %" PRIi64 "\n", outputSize);
	outPacketRefs->deserialize(llrFile);

	auto loadChunk = [&](int64_t start, int64_t end)
	{
		fprintf(stderr, "  %" PRIi64 "-%" PRIi64 ": Loading - size %" PRIi64 "\n", start, end, end - start);
		avio_seek(outputFile, start, SEEK_SET);

		while (start != end)
		{
			int64_t r = avio_read_partial(llrFile, buffer, std::min(LLR_BUFFER_SIZE, end - start));
			if (r == 0)
				errx(EXIT_FAILURE, "avio_read_partial: Premature end of file");
			else if (r < 0)
				failOnAVERROR(r, "avio_read_partial");

			fprintf(stderr, "   -> %" PRIi64 "-%" PRIi64 ": size %" PRIi64 "\n", start, start + r, r);

			avio_write(outputFile, buffer, r);
			start += r;
		}
	};

	for (const auto &[origPos, e] : outPacketRefs->table())
	{
		if (origPos != prevOffset)
		{
			loadChunk(prevOffset, origPos);
			prevOffset = origPos;
		}

		prevOffset += e.origSize;
	}

	if (prevOffset != outputSize)
		loadChunk(prevOffset, outputSize);
}
