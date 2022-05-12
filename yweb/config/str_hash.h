#pragma once

#include <library/cpp/charset/ci_string.h>
#include <util/str_stl.h>
#include <util/generic/hash_set.h>
#include <util/generic/string.h>
#include <util/system/defaults.h>

struct TConfigNameSet : public THashSet<const char*, ci_hash, ci_equal_to> {};
