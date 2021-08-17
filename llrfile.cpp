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

void PacketReferences::dump(FILE *dest) const
{
	fprintf(dest, "Packet references (total %zu):\n", m_table.size());

	for (const auto &[origPos, e] : m_table)
	{
		fprintf(dest, "  %" PRIi64 "-%" PRIi64 ": Stream #0:%d (index %zu) - pts %" PRIi64 " size %d\n",
			origPos, origPos + e.origSize, e.streamIndex, e.packetIndex, e.pts, e.origSize);
	}
}

void PacketReferences::save(AVIOContext *dest) const
{
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

void writeLLR(AVIOContext *source, const PacketReferences *packetRefs, AVIOContext *dest)
{
	unsigned char buffer[LLR_BUFFER_SIZE];

	fprintf(stderr, "Writing LLR file:\n");
	avio_wb32(dest, LLR_MAGIC_SIGNATURE);

	int64_t sourceSize = avio_size(source);
	int64_t prevOffset = 0;

	avio_wb64(dest, sourceSize);
	packetRefs->save(dest);

	auto embedChunk = [&](int64_t start, int64_t end)
	{
		fprintf(stderr, "  %" PRIi64 "-%" PRIi64 ": Embedding - size %" PRIi64 "\n", start, end, end - start);

		avio_seek(source, start, SEEK_SET);
		while (start != end)
		{
			int64_t r = avio_read_partial(source, buffer, std::min(LLR_BUFFER_SIZE, end - start));
			if (r == 0)
				errx(EXIT_FAILURE, "avio_read_partial: Premature end of file");
			else if (r < 0)
				failOnAVERROR(r, "avio_read_partial");

			fprintf(stderr, "   -> %" PRIi64 "-%" PRIi64 ": size %" PRIi64 "\n", start, start + r, r);

			avio_write(dest, buffer, r);
			start += r;
		}
	};

	for (const auto &[origPos, e] : packetRefs->m_table)
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

	if (prevOffset != sourceSize)
		embedChunk(prevOffset, sourceSize);
}
