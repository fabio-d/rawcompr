/*
 * Losslessly compress raw streams in multimedia files
 * Copyright (C) 2021  Fabio D'Urso <fabiodurso@hotmail.it>
 *
 * This program is free software; you can redistribute it and/or modify
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

#ifndef LOG_H
#define LOG_H

// Non-suppressible messages
void logError(const char *fmt, ...)  __attribute__((format(printf, 1, 2), noreturn));
void logWarning(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));

// Debug messages (only enabled if requested by the user)
void setupLogDebug(bool enable);
void logDebug(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));

#endif
