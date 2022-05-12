# -*- coding: utf-8 -*-

import yatest.common
import os


def run_one(lang, bastard_freq, detach_grammar, inp_path, opts=None):
    command = [
        yatest.common.binary_path("dict/tools/make_morphdict/main/make_morphdict"),
        "make-all",
        "--language",
        lang,
        "--bastard-freq",
        str(bastard_freq),
    ]
    if detach_grammar:
        command.append("-g")
    if opts is not None:
        command.extend(opts)
    out_path = "%s_l%s_b%d_g%d.out" % (os.path.basename(inp_path), lang, bastard_freq, detach_grammar)
    yatest.common.execute(
        command,
        stdin=open(inp_path),
        stdout=open(out_path, "w")
    )
    return yatest.common.canonical_file(out_path)

