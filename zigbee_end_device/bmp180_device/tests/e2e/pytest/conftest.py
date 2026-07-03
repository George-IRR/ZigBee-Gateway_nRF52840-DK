# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

import pytest

def pytest_addoption(parser):
    parser.addoption('--cmdopt')

@pytest.fixture()
def cmdopt(request):
    return request.config.getoption('--cmdopt')
