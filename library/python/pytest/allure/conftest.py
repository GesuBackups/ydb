import os
import pytest


@pytest.mark.tryfirst
def pytest_configure(config):
    if "ALLURE_REPORT_DIR" in os.environ:
        config.option.allurereportdir = os.environ["ALLURE_REPORT_DIR"]
