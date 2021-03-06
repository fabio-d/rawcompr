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

#include "log.h"

#include <assert.h>
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
static const std::string defaultHashName = "MD5";

static std::string defaultLibavLogLevel = "warning";
static std::map<std::string, int> libavLogLevels =
{
	{ "quiet", AV_LOG_QUIET },
	{ "panic", AV_LOG_PANIC },
	{ "fatal", AV_LOG_FATAL },
	{ "error", AV_LOG_ERROR },
	{ "warning", AV_LOG_WARNING },
	{ "info", AV_LOG_INFO },
	{ "verbose", AV_LOG_VERBOSE },
	{ "debug", AV_LOG_DEBUG },
	{ "trace", AV_LOG_TRACE }
};

static AVCodecID parseVideoCodec(const std::string &name)
{
	if (name == "ffv1")
		return AV_CODEC_ID_FFV1;
	if (name == "huffyuv")
		return AV_CODEC_ID_HUFFYUV;
	if (name == "h264")
		return AV_CODEC_ID_H264;

	logWarning("Invalid or unsupported video codec: %s\n", name.c_str());
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
			logWarning("Invalid codec option format (expected key=value): %s\n", *args);
			errorFlag = true;
			break;
		}

		if (result.emplace(key, value).second == false)
		{
			logWarning("Codec option set more than once: %s\n", key.c_str());
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

	logWarning("Argument error: %s must end with .mkv\n", argName);
	return "";
}

CommandLine::CommandLine(int argc, char *argv[])
: m_debugFlag(false), m_libavLogLevel(libavLogLevels.at(defaultLibavLogLevel)),
  m_decompressFlag(false),
  m_videoCodec(parseVideoCodec(defaultVideoCodec)), m_videoCodecOptions(defaultVideoCodecOptions), m_hashName(defaultHashName)
{
	bool seenLibavLogLevel = false;
	bool seenInputFile = false;
	bool seenOutputFile = false;
	bool seenVideoCodec = false;
	bool seenHashName = false;
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
				logWarning("Argument cannot be repeated more than once: OUTPUT\n");
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
		else if (strcmp(argv[i], "--debug") == 0)
		{
			if (m_debugFlag)
			{
				logWarning("Option cannot be repeated more than once: --debug\n");
				valid = false;
			}
			else
			{
				m_debugFlag = true;
			}
		}
		else if (strcmp(argv[i], "--libavloglevel") == 0)
		{
			if (++i >= argc)
			{
				logWarning("Argument required: --libavloglevel LEVEL\n");
				valid = false;
			}
			else if (seenLibavLogLevel)
			{
				logWarning("Option cannot be repeated more than once: --libavloglevel LEVEL\n");
				valid = false;
			}
			else
			{
				auto it = libavLogLevels.find(argv[i]);
				if (it != libavLogLevels.end())
				{
					m_libavLogLevel = it->second;
				}
				else
				{
					logWarning("Invalid libav log level: %s\n", argv[i]);
					valid = false;
				}
			}

			seenLibavLogLevel = true;
		}
		else if (strcmp(argv[i], "-d") == 0)
		{
			if (m_decompressFlag)
			{
				logWarning("Option cannot be repeated more than once: -d\n");
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
				logWarning("Argument required: -i INPUT\n");
				valid = false;
			}
			else if (seenInputFile)
			{
				logWarning("Option cannot be repeated more than once: -i INPUT\n");
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
				logWarning("Argument required: -v CODEC_NAME [key=value ...]\n");
				valid = false;
			}
			else if (seenVideoCodec)
			{
				logWarning("Option cannot be repeated more than once: -v CODEC_NAME [key=value ...]\n");
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
		else if (strcmp(argv[i], "--hash") == 0)
		{
			if (++i >= argc)
			{
				logWarning("Argument required: --hash ALGORITHM\n");
				valid = false;
			}
			else if (seenHashName)
			{
				logWarning("Option cannot be repeated more than once: --hash ALGORITHM\n");
				valid = false;
			}
			else
			{
				m_hashName = argv[i];

				bool found = false;
				for (const std::string &e : enumerateHashAlgorithms())
				{
					if (e == m_hashName)
						found = true;
				}

				if (!found)
				{
					logWarning("Invalid hash algorithm: %s\n", argv[i]);
					valid = false;
				}
			}

			seenHashName = true;
		}
		else if (strcmp(argv[i], "--") == 0)
		{
			seenDoubleDash = true;
		}
		else if (argv[i][0] == '-')
		{
			logWarning("Invalid option: %s\n", argv[i]);
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
			logWarning("Option can only be used if -d is not set: -v CODEC_NAME [key=value ...]\n");
			valid = false;
		}

		if (seenHashName)
		{
			logWarning("Option can only be used if -d is not set: --hash ALGORITHM\n");
			valid = false;
		}
	}

	if (!seenInputFile)
	{
		logWarning("Missing required option: -i INPUT\n");
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
		logWarning("Missing required option: OUTPUT\n");
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
	fprintf(stderr, "Losslessly compress raw streams in multimedia files.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage: %s [-d] [OTHER OPTIONS] -i INPUT OUTPUT\n", program_invocation_short_name);
	fprintf(stderr, "\n");

	fprintf(stderr, "Basic options:\n");
	fprintf(stderr, " -d        Decompress instead of compressing\n");
	fprintf(stderr, " -i INPUT  Input file\n");
	fprintf(stderr, " OUTPUT    Output file\n");
	fprintf(stderr, " --debug   Enable debug output from rawcompr\n");
	fprintf(stderr, " --libavloglevel LEVEL\n");
	fprintf(stderr, "           Set libav log level\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "Compression-only parameters:\n");
	fprintf(stderr, " -v CODEC_NAME [key=value ...]\n");
	fprintf(stderr, "           Select video codec and options\n");
	fprintf(stderr, " --hash ALGORITHM\n");
	fprintf(stderr, "           Embed the input file's hash using the selected algorithm (default: %s)\n", defaultHashName.c_str());
	fprintf(stderr, "\n");

	fprintf(stderr, "Note:\n");
	fprintf(stderr, " - If compressing, OUTPUT file must have .mkv extension\n");
	fprintf(stderr, " - If decompressing, INPUT file must have .mkv extension\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "Default video codec: -v %s", defaultVideoCodec.c_str());
	for (const auto &[k, v] : defaultVideoCodecOptions)
		fprintf(stderr, " %s=%s", k.c_str(), v.c_str());
	fprintf(stderr, "\n");

	fprintf(stderr, "Available hash algorithms:");
	for (const std::string &e : enumerateHashAlgorithms())
		fprintf(stderr, " %s", e.c_str());
	fprintf(stderr, "\n");
}

bool CommandLine::enableLogDebug() const
{
	return m_debugFlag;
}

int CommandLine::libavLogLevel() const
{
	return m_libavLogLevel;
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

std::string CommandLine::hashName() const
{
	assert(m_decompressFlag == false);

	return m_hashName;
}
