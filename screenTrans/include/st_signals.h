// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <cstdint>

// We can use enum and define a bunch of functions which is small in size to support a more modern mode, but I reject. It's a stupied waste in most aspects.

using Signal = int8_t;
constexpr inline Signal signal_none				= 0;
constexpr inline Signal signal_closed			= Signal(1) << 0;
constexpr inline Signal signal_choiceNotMatch	= Signal(1) << 1;

using Choice = int;
constexpr inline Choice choice_invalid		= -1;
constexpr inline Choice choice_enter_room	= 0;
constexpr inline Choice choice_make_room	= 1;

constexpr inline size_t words_msg_bytes = 256;

using Connect_t = uint8_t;
constexpr inline Connect_t connect_user	= 0;
constexpr inline Connect_t connect_audio= 1;

using faudio_t = uint8_t;
constexpr inline faudio_t faudio_none			= 0;
constexpr inline faudio_t faudio_share			= faudio_t(1) << 0;
constexpr inline faudio_t faudio_mute_all_once	= faudio_t(1) << 1;
constexpr inline faudio_t faudio_mute_all_keep	= faudio_t(1) << 2;
constexpr inline faudio_t faudio_is_host		= faudio_t(1) << 3;