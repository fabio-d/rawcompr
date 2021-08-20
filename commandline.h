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

#ifndef COMMANDLINE_H
#define COMMANDLINE_H

#include "libav.h"

#include <map>
#include <string>

class CommandLine
{
	public:
		enum Operation
		{
			Compress,
			Decompress
		};

		CommandLine(int argc, char *argv[]);

		bool enableLogDebug() const;

		Operation operation() const;

		const char *inputFile() const;
		const char *outputFile() const;
		const char *llrFile() const;

		AVCodecID videoCodec() const;
		void fillVideoCodecOptions(AVDictionary **outDict) const;
		std::string hashName() const;

	private:
		void help();

		bool m_debugFlag, m_decompressFlag;
		std::string m_inputFile, m_outputFile, m_llrFile;

		AVCodecID m_videoCodec;
		std::map<std::string, std::string> m_videoCodecOptions;
		std::string m_hashName;
};

#endif
