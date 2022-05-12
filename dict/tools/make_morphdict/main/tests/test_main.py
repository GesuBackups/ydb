# -*- coding: utf-8 -*-

from common import run_one


def test_eng():
    return run_one("eng", 200, True, "eng-lister.txt")


def test_eng_proto():
    return run_one("eng", 200, True, "eng-lister.txt", ["--make-proto-dict"])
