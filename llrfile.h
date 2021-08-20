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

#ifndef LLRFILE_H
#define LLRFILE_H

#include "libav.h"

#include <map>
#include <string>
#include <vector>

enum CodecType : char // These values are stored on-disk in LLR files
{
	Copy = 1,
	Video = 2
};

class PacketReferences
{
	public:
		struct StreamInfo
		{
			CodecType type;
			std::string pixelFormat;
		};

		struct ReferenceInfo
		{
			// Length of covered range in original file
			int origSize;

			// Reference to encoded packet in compressed file
			int streamIndex;
			size_t packetIndex;
			int64_t pts;
		};

		void addVideoStream(AVPixelFormat pixelFormat);
		void addCopyStream();

		void addPacketReference(int streamIndex, size_t packetIndex, int64_t pts, int64_t origPos, int origSize);

		const std::vector<StreamInfo> &streams() const;
		const std::map<size_t, ReferenceInfo> &table() const;

		void debugDump() const;

		void deserialize(AVIOContext *src);
		void serialize(AVIOContext *dest) const;

	private:
		std::vector<StreamInfo> m_streams;
		std::map<size_t, ReferenceInfo> m_table; // origPos -> other fields
};

void writeLLR(AVIOContext *inputFile, const PacketReferences *packetRefs, AVIOContext *llrFile);
void readLLR(AVIOContext *llrFile, PacketReferences *outPacketRefs, AVIOContext *outputFile);

#endif
