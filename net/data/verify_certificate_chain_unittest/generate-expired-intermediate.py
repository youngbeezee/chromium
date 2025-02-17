#!/usr/bin/python
# Copyright (c) 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Certificate chain with 1 intermediate, where the intermediate is expired
(violates validity.notAfter). Verification is expected to fail."""

import common

# Self-signed root certificate (used as trust anchor).
root = common.create_self_signed_root_certificate('Root')
root.set_validity_range(common.JANUARY_1_2015_UTC, common.JANUARY_1_2016_UTC)

# Intermediate certificate.
intermediate = common.create_intermediate_certificate('Intermediate', root)
intermediate.set_validity_range(common.JANUARY_1_2015_UTC,
                                common.MARCH_1_2015_UTC)

# Target certificate.
target = common.create_end_entity_certificate('Target', intermediate)
target.set_validity_range(common.JANUARY_1_2015_UTC, common.JANUARY_1_2016_UTC)

chain = [target, intermediate]
trusted = common.TrustAnchor(root, constrained=False)

# Both the root and target are valid at this time, however the
# intermediate certificate is not.
time = common.MARCH_2_2015_UTC
key_purpose = common.DEFAULT_KEY_PURPOSE
verify_result = False
errors = """----- Certificate i=1 (CN=Intermediate) -----
ERROR: Time is after notAfter

"""

common.write_test_file(__doc__, chain, trusted, time, key_purpose,
                       verify_result, errors)
