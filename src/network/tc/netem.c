/* SPDX-License-Identifier: LGPL-2.1+
 * Copyright © 2019 VMware, Inc. */

#include <linux/pkt_sched.h>

#include "alloc-util.h"
#include "conf-parser.h"
#include "netem.h"
#include "netlink-util.h"
#include "networkd-manager.h"
#include "parse-util.h"
#include "qdisc.h"
#include "strv.h"
#include "tc-util.h"

static int network_emulator_fill_message(Link *link, QDisc *qdisc, sd_netlink_message *req) {
        struct tc_netem_qopt opt = {
               .limit = 1000,
        };
        NetworkEmulator *ne;
        int r;

        assert(link);
        assert(qdisc);
        assert(req);

        ne = NETEM(qdisc);

        if (ne->limit > 0)
                opt.limit = ne->limit;

        if (ne->loss > 0)
                opt.loss = ne->loss;

        if (ne->duplicate > 0)
                opt.duplicate = ne->duplicate;

        if (ne->delay != USEC_INFINITY) {
                r = tc_time_to_tick(ne->delay, &opt.latency);
                if (r < 0)
                        return log_link_error_errno(link, r, "Failed to calculate latency in TCA_OPTION: %m");
        }

        if (ne->jitter != USEC_INFINITY) {
                r = tc_time_to_tick(ne->jitter, &opt.jitter);
                if (r < 0)
                        return log_link_error_errno(link, r, "Failed to calculate jitter in TCA_OPTION: %m");
        }

        r = sd_netlink_message_append_data(req, TCA_OPTIONS, &opt, sizeof(struct tc_netem_qopt));
        if (r < 0)
                return log_link_error_errno(link, r, "Could not append TCA_OPTION attribute: %m");

        return 0;
}

int config_parse_network_emulator_delay(
                const char *unit,
                const char *filename,
                unsigned line,
                const char *section,
                unsigned section_line,
                const char *lvalue,
                int ltype,
                const char *rvalue,
                void *data,
                void *userdata) {

        _cleanup_(qdisc_free_or_set_invalidp) QDisc *qdisc = NULL;
        Network *network = data;
        NetworkEmulator *ne;
        usec_t u;
        int r;

        assert(filename);
        assert(lvalue);
        assert(rvalue);
        assert(data);

        r = qdisc_new_static(QDISC_KIND_NETEM, network, filename, section_line, &qdisc);
        if (r == -ENOMEM)
                return log_oom();
        if (r < 0)
                return log_syntax(unit, LOG_ERR, filename, line, r,
                                  "More than one kind of queueing discipline, ignoring assignment: %m");

        ne = NETEM(qdisc);

        if (isempty(rvalue)) {
                if (STR_IN_SET(lvalue, "DelaySec", "NetworkEmulatorDelaySec"))
                        ne->delay = USEC_INFINITY;
                else if (STR_IN_SET(lvalue, "DelayJitterSec", "NetworkEmulatorDelayJitterSec"))
                        ne->jitter = USEC_INFINITY;

                qdisc = NULL;
                return 0;
        }

        r = parse_sec(rvalue, &u);
        if (r < 0) {
                log_syntax(unit, LOG_ERR, filename, line, r,
                           "Failed to parse '%s=', ignoring assignment: %s",
                           lvalue, rvalue);
                return 0;
        }

        if (STR_IN_SET(lvalue, "DelaySec", "NetworkEmulatorDelaySec"))
                ne->delay = u;
        else if (STR_IN_SET(lvalue, "DelayJitterSec", "NetworkEmulatorDelayJitterSec"))
                ne->jitter = u;

        qdisc = NULL;

        return 0;
}

int config_parse_network_emulator_rate(
                const char *unit,
                const char *filename,
                unsigned line,
                const char *section,
                unsigned section_line,
                const char *lvalue,
                int ltype,
                const char *rvalue,
                void *data,
                void *userdata) {

        _cleanup_(qdisc_free_or_set_invalidp) QDisc *qdisc = NULL;
        Network *network = data;
        NetworkEmulator *ne;
        uint32_t rate;
        int r;

        assert(filename);
        assert(lvalue);
        assert(rvalue);
        assert(data);

        r = qdisc_new_static(QDISC_KIND_NETEM, network, filename, section_line, &qdisc);
        if (r == -ENOMEM)
                return log_oom();
        if (r < 0)
                return log_syntax(unit, LOG_ERR, filename, line, r,
                                  "More than one kind of queueing discipline, ignoring assignment: %m");

        ne = NETEM(qdisc);

        if (isempty(rvalue)) {
                if (STR_IN_SET(lvalue, "LossRate", "NetworkEmulatorLossRate"))
                        ne->loss = 0;
                else if (STR_IN_SET(lvalue, "DuplicateRate", "NetworkEmulatorDuplicateRate"))
                        ne->duplicate = 0;

                qdisc = NULL;
                return 0;
        }

        r = parse_tc_percent(rvalue, &rate);
        if (r < 0) {
                log_syntax(unit, LOG_ERR, filename, line, r,
                           "Failed to parse '%s=', ignoring assignment: %s",
                           lvalue, rvalue);
                return 0;
        }

        if (STR_IN_SET(lvalue, "LossRate", "NetworkEmulatorLossRate"))
                ne->loss = rate;
        else if (STR_IN_SET(lvalue, "DuplicateRate", "NetworkEmulatorDuplicateRate"))
                ne->duplicate = rate;

        qdisc = NULL;
        return 0;
}

int config_parse_network_emulator_packet_limit(
                const char *unit,
                const char *filename,
                unsigned line,
                const char *section,
                unsigned section_line,
                const char *lvalue,
                int ltype,
                const char *rvalue,
                void *data,
                void *userdata) {

        _cleanup_(qdisc_free_or_set_invalidp) QDisc *qdisc = NULL;
        Network *network = data;
        NetworkEmulator *ne;
        int r;

        assert(filename);
        assert(lvalue);
        assert(rvalue);
        assert(data);

        r = qdisc_new_static(QDISC_KIND_NETEM, network, filename, section_line, &qdisc);
        if (r == -ENOMEM)
                return log_oom();
        if (r < 0)
                return log_syntax(unit, LOG_ERR, filename, line, r,
                                  "More than one kind of queueing discipline, ignoring assignment: %m");

        ne = NETEM(qdisc);

        if (isempty(rvalue)) {
                ne->limit = 0;
                qdisc = NULL;

                return 0;
        }

        r = safe_atou(rvalue, &ne->limit);
        if (r < 0) {
                log_syntax(unit, LOG_ERR, filename, line, r,
                           "Failed to parse '%s=', ignoring assignment: %s",
                           lvalue, rvalue);
                return 0;
        }

        qdisc = NULL;
        return 0;
}

const QDiscVTable netem_vtable = {
        .object_size = sizeof(NetworkEmulator),
        .tca_kind = "netem",
        .fill_message = network_emulator_fill_message,
};
