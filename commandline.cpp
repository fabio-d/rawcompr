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

#include <assert.h>
#include <err.h>
#include <string.h>

static const std::string defaultVideoCodec = "ffv1";
static const std::map<std::string, std::string> defaultVideoCodecOptions
{
	{ "level", "3" },
	{ "slicecrc", "0" },
	{ "context", "1" },
	{ "coder", "range_def" },
	{ "g", "600" },
	{ "slices", "4" }
};

static AVCodecID parseVideoCodec(const std::string &name)
{
	if (name == "ffv1")
		return AV_CODEC_ID_FFV1;
	if (name == "huffyuv")
		return AV_CODEC_ID_HUFFYUV;

	warnx("Invalid or unsupported video codec: %s", name.c_str());
	return AV_CODEC_ID_NONE;
}

static std::pair<std::map<std::string, std::string>, bool> parseCodecOptions(char *args[], size_t count)
{
	std::map<std::string, std::string> result;
	bool errorFlag = false;

	while (count-- != 0)
	{
		char *eqSign = strchr(*args, '=');
		if (eqSign == nullptr)
			break;

		std::string key(*args, eqSign), value(eqSign + 1);
		if (key.empty() || value.empty())
		{
			warnx("Invalid codec option format (expected key=value): %s", *args);
			errorFlag = true;
			break;
		}

		if (result.emplace(key, value).second == false)
		{
			warnx("Codec option set more than once: %s", key.c_str());
			errorFlag = true;
			break;
		}

		args++;
	}

	return { result, errorFlag };
}

static std::string llrFileFromMkv(const char *argName, const std::string &argValue)
{
	if (argValue.length() >= 4 && argValue.substr(argValue.length() - 4) == ".mkv")
		return argValue.substr(0, argValue.length() - 4) + ".llr";

	warnx("Argument error: %s must end with .mkv", argName);
	return "";
}

CommandLine::CommandLine(int argc, char *argv[])
: m_decompressFlag(false),
  m_videoCodec(parseVideoCodec(defaultVideoCodec)), m_videoCodecOptions(defaultVideoCodecOptions)
{
	bool seenInputFile = false;
	bool seenOutputFile = false;
	bool seenVideoCodec = false;
	bool seenDoubleDash = false;
	bool valid = true;

	if (argc <= 1)
	{
		help();
		exit(EXIT_FAILURE);
	}

	for (int i = 1; i < argc; i++)
	{
		if (seenDoubleDash) // current option is after "--"
		{
process_positional_argument:
			if (seenOutputFile)
			{
				warnx("Argument cannot be repeated more than once: OUTPUT");
			}
			else
			{
				m_outputFile = argv[i];
				seenOutputFile = true;
			}
		}
		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0)
		{
			help();
			exit(EXIT_SUCCESS);
		}
		else if (strcmp(argv[i], "-d") == 0)
		{
			if (m_decompressFlag)
			{
				warnx("Option cannot be repeated more than once: -d");
				valid = false;
			}
			else
			{
				m_decompressFlag = true;
			}
		}
		else if (strcmp(argv[i], "-i") == 0)
		{
			if (++i >= argc)
			{
				warnx("Argument required: -i INPUT");
				valid = false;
			}
			else if (seenInputFile)
			{
				warnx("Option cannot be repeated more than once: -i INPUT");
				valid = false;
			}
			else
			{
				m_inputFile = argv[i];
			}

			seenInputFile = true;
		}
		else if (strcmp(argv[i], "-v") == 0)
		{
			if (++i >= argc)
			{
				warnx("Argument required: -v CODEC_NAME [key=value ...]");
				valid = false;
			}
			else if (seenVideoCodec)
			{
				warnx("Option cannot be repeated more than once: -v CODEC_NAME [key=value ...]");
				valid = false;
			}
			else
			{
				m_videoCodec = parseVideoCodec(argv[i]);

				bool errorFlag;
				std::tie(m_videoCodecOptions, errorFlag) = parseCodecOptions(argv + i + 1, argc - i - 1);

				if (m_videoCodec == AV_CODEC_ID_NONE || errorFlag)
					valid = false;
				i += m_videoCodecOptions.size();
			}

			seenVideoCodec = true;
		}
		else if (strcmp(argv[i], "--") == 0)
		{
			seenDoubleDash = true;
		}
		else if (argv[i][0] == '-')
		{
			warnx("Invalid option: %s", argv[i]);
			valid = false;
		}
		else
		{
			goto process_positional_argument;
		}
	}

	if (m_decompressFlag)
	{
		if (seenVideoCodec)
		{
			warnx("Option can only be used if -d is not set: -v CODEC_NAME [key=value ...]");
			valid = false;
		}
	}

	if (!seenInputFile)
	{
		warnx("Missing required option: -i INPUT");
		valid = false;
	}
	else if (m_decompressFlag)
	{
		m_llrFile = llrFileFromMkv("INPUT", m_inputFile);
		if (m_llrFile.empty())
			valid = false;
	}

	if (!seenOutputFile)
	{
		warnx("Missing required option: OUTPUT");
		valid = false;
	}
	else if (!m_decompressFlag)
	{
		m_llrFile = llrFileFromMkv("OUTPUT", m_outputFile);
		if (m_llrFile.empty())
			valid = false;
	}

	if (!valid)
		exit(EXIT_FAILURE);
}

void CommandLine::help()
{
	fprintf(stderr, "Losslessly compress raw streams in multimedia files\n");
	fprintf(stderr, "Usage: %s [-d] [CODEC PARAMETERS] -i INPUT OUTPUT\n", program_invocation_short_name);
	fprintf(stderr, "\n");

	fprintf(stderr, "Basic options:\n");
	fprintf(stderr, " -d        Decompress instead of compressing\n");
	fprintf(stderr, " -i INPUT  Input file\n");
	fprintf(stderr, " OUTPUT    Output file\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "Codec parameters (compression only):\n");
	fprintf(stderr, " -v CODEC_NAME [key=value ...]\n");
	fprintf(stderr, "           Select video codec and options\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "Note:\n");
	fprintf(stderr, " - If compressing, OUTPUT file must have .mkv extension\n");
	fprintf(stderr, " - If decompressing, INPUT file must have .mkv extension\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "Default video codec: -v %s", defaultVideoCodec.c_str());
	for (const auto &[k, v] : defaultVideoCodecOptions)
		fprintf(stderr, " %s=%s", k.c_str(), v.c_str());
	fprintf(stderr, "\n");
}

CommandLine::Operation CommandLine::operation() const
{
	return m_decompressFlag ? Decompress : Compress;
}

const char *CommandLine::inputFile() const
{
	return m_inputFile.c_str();
}

const char *CommandLine::outputFile() const
{
	return m_outputFile.c_str();
}

const char *CommandLine::llrFile() const
{
	return m_llrFile.c_str();
}

AVCodecID CommandLine::videoCodec() const
{
	assert(m_decompressFlag == false);
	return m_videoCodec;
}

void CommandLine::fillVideoCodecOptions(AVDictionary **outDict) const
{
	assert(m_decompressFlag == false);

	for (auto &[k, v] : m_videoCodecOptions)
		av_dict_set(outDict, k.c_str(), v.c_str(), 0);
}
