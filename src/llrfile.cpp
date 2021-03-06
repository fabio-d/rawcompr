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

#include "log.h"

#include <err.h>
#include <inttypes.h>

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
		logError("addPacketReference: overlapping range, probably a bug. halting!\n");
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

void PacketReferences::debugDump() const
{
	logDebug("Streams (total %zu):\n", m_streams.size());

	for (size_t i = 0; i < m_streams.size(); i++)
	{
		const StreamInfo &info = m_streams.at(i);
		logDebug("  Stream #0:%zu: ", i);

		switch (info.type)
		{
			case Video:
				logDebug("video %s\n", info.pixelFormat.c_str());
				break;
			case Copy:
				logDebug("copy\n");
				break;
			default:
				abort();
		}
	}

	logDebug("Packet references (total %zu):\n", m_table.size());

	for (const auto &[origPos, e] : m_table)
	{
		logDebug("  %" PRIi64 "-%" PRIi64 ": Stream #0:%d (index %zu) - pts %" PRIi64 " size %d\n",
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
	failOnWriteError(avio_wb32, dest, m_streams.size());
	for (const StreamInfo &e : m_streams)
	{
		failOnWriteError(avio_w8, dest, e.type);

		switch (e.type)
		{
			case Video:
			{
				failOnWriteError(avio_put_str, dest, e.pixelFormat.c_str());
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

	failOnWriteError(avio_wb64, dest, m_table.size());
	for (const auto &[origPos, e] : m_table)
	{
		failOnWriteError(avio_wb64, dest, origPos);
		failOnWriteError(avio_wb32, dest, e.origSize);
		failOnWriteError(avio_wb32, dest, e.streamIndex);
		failOnWriteError(avio_wb64, dest, e.packetIndex);
		failOnWriteError(avio_wb64, dest, e.pts);
	}
}

void writeLLR(AVIOContext *inputFile, const PacketReferences *packetRefs, AVIOContext *llrFile, const char *hashName)
{
	unsigned char buffer[LLR_BUFFER_SIZE];

	logDebug("Writing LLR file:\n");
	failOnWriteError(avio_wb32, llrFile, LLR_MAGIC_SIGNATURE);

	int64_t inputSize = avio_size(inputFile);
	int64_t prevOffset = 0;

	failOnWriteError(avio_wb64, llrFile, inputSize);

	// Initialize hashing
	AVHashContext *hashCtx;
	failOnAVERROR(av_hash_alloc(&hashCtx, hashName), "av_hash_alloc");
	av_hash_init(hashCtx);
	int hashSize = av_hash_get_size(hashCtx);

	// Store hash name and size in output file + reserve space for the final hash
	failOnWriteError(avio_put_str, llrFile, hashName);
	failOnWriteError(avio_wb16, llrFile, hashSize);
	int64_t hashPos = avio_tell(llrFile);
	seekOrFail(llrFile, hashPos + hashSize);

	packetRefs->serialize(llrFile);

	seekOrFail(inputFile, 0);

	auto embedChunk = [&](int64_t start, int64_t end)
	{
		logDebug("  %" PRIi64 "-%" PRIi64 ": Embedding - size %" PRIi64 "\n", start, end, end - start);

		if (avio_tell(inputFile) != start)
			logError("embedChunk: Unexpected file offset, probably a bug. halting!\n");

		while (start != end)
		{
			int64_t r = avio_read_partial(inputFile, buffer, std::min(LLR_BUFFER_SIZE, end - start));
			if (r == 0)
				logError("avio_read_partial: Premature end of file\n");
			else if (r < 0)
				failOnAVERROR(r, "avio_read_partial");

			logDebug("   -> %" PRIi64 "-%" PRIi64 ": size %" PRIi64 "\n", start, start + r, r);

			failOnWriteError(avio_write, llrFile, buffer, r);
			av_hash_update(hashCtx, buffer, r);

			start += r;
		}
	};

	auto hashChunk = [&](int64_t start, int64_t end)
	{
		if (avio_tell(inputFile) != start)
			logError("hashChunk: Unexpected file offset, probably a bug. halting!\n");

		while (start != end)
		{
			int64_t r = avio_read_partial(inputFile, buffer, std::min(LLR_BUFFER_SIZE, end - start));
			if (r == 0)
				logError("avio_read_partial: Premature end of file\n");
			else if (r < 0)
				failOnAVERROR(r, "avio_read_partial");

			logDebug("   -> %" PRIi64 "-%" PRIi64 ": size %" PRIi64 "\n", start, start + r, r);

			av_hash_update(hashCtx, buffer, r);

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

		logDebug("  %" PRIi64 "-%" PRIi64 ": Referencing stream #0:%d (index %zu) - pts %" PRIi64 " size %d\n",
			origPos, prevOffset, e.streamIndex, e.packetIndex, e.pts, e.origSize);

		hashChunk(origPos, prevOffset);
	}

	if (prevOffset != inputSize)
		embedChunk(prevOffset, inputSize);

	// Finalize hashing and write result
	uint8_t hashBuffer[hashSize];
	av_hash_final(hashCtx, hashBuffer);
	av_hash_freep(&hashCtx);

	logDebug("Storing input file hash (%s): ", hashName);
	for (int i = 0; i < hashSize; i++)
		logDebug("%02x", hashBuffer[i]);
	logDebug("\n");

	seekOrFail(llrFile, hashPos);
	failOnWriteError(avio_write, llrFile, hashBuffer, hashSize);
}

LLRInfo readLLRInfo(AVIOContext *llrFile)
{
	LLRInfo result;

	if (avio_rb32(llrFile) != LLR_MAGIC_SIGNATURE)
		logError("Invalid LLR file signature\n");

	logDebug("Reading LLR file:\n");

	result.originalFileSize = avio_rb64(llrFile);
	logDebug("  Original file size: %" PRIi64 "\n", result.originalFileSize);

	char buffer[128];
	avio_get_str(llrFile, sizeof(buffer) - 1, buffer, sizeof(buffer));
	result.hashName = buffer;

	int hashSize = avio_rb16(llrFile);
	logDebug("  Hash: %s (size %d) ", result.hashName.c_str(), hashSize);

	std::vector<uint8_t> hashBuffer(hashSize);
	avio_read(llrFile, hashBuffer.data(), hashSize);
	for (int i = 0; i < hashSize; i++)
		logDebug("%02x", hashBuffer[i]);
	logDebug("\n");

	result.hashBuffer = hashBuffer;
	return result;
}

LLRInfo readLLR(AVIOContext *llrFile, PacketReferences *outPacketRefs, AVIOContext *outputFile)
{
	unsigned char buffer[LLR_BUFFER_SIZE];

	LLRInfo info = readLLRInfo(llrFile);
	outPacketRefs->deserialize(llrFile);

	auto loadChunk = [&](int64_t start, int64_t end)
	{
		logDebug("  %" PRIi64 "-%" PRIi64 ": Loading - size %" PRIi64 "\n", start, end, end - start);
		seekOrFail(outputFile, start);

		while (start != end)
		{
			int64_t r = avio_read_partial(llrFile, buffer, std::min(LLR_BUFFER_SIZE, end - start));
			if (r == 0)
				logError("avio_read_partial: Premature end of file\n");
			else if (r < 0)
				failOnAVERROR(r, "avio_read_partial");

			logDebug("   -> %" PRIi64 "-%" PRIi64 ": size %" PRIi64 "\n", start, start + r, r);

			failOnWriteError(avio_write, outputFile, buffer, r);
			start += r;
		}
	};

	int64_t prevOffset = 0;

	for (const auto &[origPos, e] : outPacketRefs->table())
	{
		if (origPos != prevOffset)
		{
			loadChunk(prevOffset, origPos);
			prevOffset = origPos;
		}

		prevOffset += e.origSize;
	}

	if (prevOffset != info.originalFileSize)
		loadChunk(prevOffset, info.originalFileSize);

	return info;
}
