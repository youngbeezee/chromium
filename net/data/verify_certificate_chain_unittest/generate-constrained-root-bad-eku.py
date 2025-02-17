#!/usr/bin/python
# Copyright (c) 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Certificate chain with 1 intermediate and a trust anchor. The trust anchor
has an EKU that restricts it to clientAuth. Verification is expected to fail as
the end-entity is verified for serverAuth, and the trust anchor enforces
constraints."""

import common

# Self-signed root certificate (used as trust anchor) with non-CA basic
# constraints.
root = common.create_self_signed_root_certificate('Root')
root.get_extensions().set_property('extendedKeyUsage', 'clientAuth')

# Intermediate certificate.
intermediate = common.create_intermediate_certificate('Intermediate', root)

# Target certificate.
target = common.create_end_entity_certificate('Target', intermediate)

chain = [target, intermediate]
trusted = common.TrustAnchor(root, constrained=True)
time = common.DEFAULT_TIME
key_purpose = common.KEY_PURPOSE_SERVER_AUTH
verify_result = False
errors = """----- Certificate i=2 (CN=Root) -----
ERROR: The extended key usage does not include server auth

"""

common.write_test_file(__doc__, chain, trusted, time, key_purpose,
                       verify_result, errors)
