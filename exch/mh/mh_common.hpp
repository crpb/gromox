// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2021-2024 grommunio GmbH
// This file is part of Gromox.
#pragma once
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <libHX/string.h>
#include <gromox/clock.hpp>
#include <gromox/defs.h>
#include <gromox/ext_buffer.hpp>
#include <gromox/hpm_common.h>
#include <gromox/mapidefs.h>

namespace hpm_mh {

static constexpr gromox::time_duration
	response_pending_period = std::chrono::seconds(30),
	session_valid_interval = std::chrono::seconds(900),
	session_valid_extragrace = std::chrono::seconds(60);
using wallclock = std::chrono::system_clock;

struct session_data {
	session_data() = default;
	session_data(const GUID &sesguid, const GUID &seqguid,
	    const char *user, gromox::time_point exptime) :
		session_guid(sesguid), sequence_guid(seqguid), expire_time(exptime)
	{
		gx_strlcpy(username, user, UADDR_SIZE);
		HX_strlower(username);
	}
	NOMOVE(session_data);

	GUID session_guid{}, sequence_guid{};
	char username[UADDR_SIZE]{};
	gromox::time_point expire_time;
};

enum class resp_code {
	success, invalid_verb, invalid_ctx_cookie, missing_header, no_priv,
	invalid_rq_body, missing_cookie, invalid_seq, invalid_rq_type,
};

static constexpr const char *g_error_text[] = {
    "The request was properly formatted and accepted.",
    "The request has an invalid verb.",
    "The request has an invalid session context cookie.",
    "The request has a missing required header.",
    "The client has no privileges to the Session Context.",
    "The request body is invalid.",
    "The request is missing a required cookie.",
    "The request has violated the sequencing requirement"
        " of one request at a time per Session Context.",
    "Invalid request type for this endpoint.",
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern std::string render_content(wallclock::time_point, wallclock::time_point);
extern std::string commonHeader(const char *rq_type, const char *rq_id, const char *cl_info, const char *sid, const std::string &excver, wallclock::time_point);

struct MhContext
{
	bool loadHeaders();
	bool getHeader(char*, size_t);

	http_status error_responsecode(resp_code) const;
	http_status ping_response() const;
	http_status failure_response(uint32_t) const;
	http_status normal_response() const;
	http_status notification_response() const;
	http_status notification_response(uint32_t, uint32_t) const;

	int ID = 0;
	HTTP_REQUEST& orig;
	HTTP_AUTH_INFO auth_info{};

	gromox::time_point start_time{};
	wallclock::time_point wall_start_time{};
	GUID session_guid{}, sequence_guid{};
	const char *request_id = nullptr, *client_info = nullptr, *cl_app = nullptr;
	char request_value[32]{}, session_string[64]{};
	const char *user_agent = nullptr;
	size_t push_buff_size = 512 << 10;
	std::unique_ptr<char[]> push_buff;
	session_data *session = nullptr;
	std::string m_server_version;

protected:
	MhContext(int, http_request &, HTTP_AUTH_INFO &&, const std::string &);
	~MhContext() = default;
	NOMOVE(MhContext);

	EXT_PUSH *epush = nullptr;
	http_status (*write_response)(unsigned int, const void *, size_t) = nullptr;
};

}
