# -*- coding: utf-8 -*-

from common import run_one
import yatest.common


def test_rus_fixlist():
    return run_one("rus", 1, True, yatest.common.source_path("dict/tools/make_morphdict/main/tests/fix_rus.txt"))


def test_ukr_fixlist():
    return run_one("ukr", 1, True, yatest.common.source_path("dict/tools/make_morphdict/main/tests/fix_ukr.txt"))


def test_eng_fixlist():
    return run_one("eng", 1, True, yatest.common.source_path("dict/tools/make_morphdict/main/tests/fix_eng.txt"))


def test_rus10_fixlist():
    return run_one("rus", 10, True, yatest.common.source_path("dict/tools/make_morphdict/main/tests/fix_rus.txt"))


def test_ukr10_fixlist():
    return run_one("ukr", 10, True, yatest.common.source_path("dict/tools/make_morphdict/main/tests/fix_ukr.txt"))


def test_eng10_fixlist():
    return run_one("erng", 10, True, yatest.common.source_path("dict/tools/make_morphdict/main/tests/fix_eng.txt"))


def test_rus_nodetach_fixlist():
    return run_one("rus", 1, False, yatest.common.source_path("dict/tools/make_morphdict/main/tests/fix_rus.txt"))


def test_ukr_nodetach_fixlist():
    return run_one("ukr", 1, False, yatest.common.source_path("dict/tools/make_morphdict/main/tests/fix_ukr.txt"))


def test_eng_nodetach_fixlist():
    return run_one("erng", 1, False, yatest.common.source_path("dict/tools/make_morphdict/main/tests/fix_eng.txt"))
