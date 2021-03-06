#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2018 Petr Vorel <pvorel@suse.cz>
# Copyright (c) 2017 Oracle and/or its affiliates. All Rights Reserved.
# Author: Alexey Kodanev <alexey.kodanev@oracle.com>

TST_NEEDS_TMPDIR=1
TST_SETUP=tst_ipsec_setup_vti
TST_CLEANUP=tst_ipsec_cleanup
TST_TESTFUNC=do_test
. ipsec_lib.sh

do_test()
{
	tst_netload -H $ip_rmt_tun -T sctp -n $2 -N $2 -r $IPSEC_REQUESTS \
		-S $ip_loc_tun
}

tst_run
