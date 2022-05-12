# coding=utf-8
# input: pure in NEW format (use tools/pure_compiler -d --format NEW)

import sys
import codecs
try:
    from tqdm import tqdm
except ImportError:
    tqdm = lambda x: x
from collections import Counter
import math


KAZ_LETTERS = u'АаӘәБбВвГгҒғДдЕеЁёЖжЗзИиЙйКкҚқЛлМмНнҢңОоӨөПпРрСсТтУуҰұҮүФфХхҺһЦцЧчШшЩщЪъЫыІіЬьЭэЮюЯя-'
RUS_LETTERS = u'йцукенгшщзхъфывапролджэячсмитьбю-'
LETTERS = {'rus': set(RUS_LETTERS), 'kaz': KAZ_LETTERS}

def main():
    LANG = sys.argv[1]
    ngrams = {4: Counter(), 5: Counter()}
    for line in tqdm(codecs.getreader('utf8')(sys.stdin), total=11244527):
        fields = line.rstrip('\n').split('\t')
        # if len(fields) != 4 or False and (fields[3] != 'form' or fields[1] != LANG) or any(c not in RUS_LETTERS for c in fields[0]):
        if len(fields) < 3 or not fields[0].strip() or any(c not in LETTERS[LANG] for c in fields[0]):
            continue
        freq = int(fields[2])
        word = ' ' + fields[0]
        for n in [4, 5]:
            if len(word) < n:
                ngrams[n][word] += freq
            else:
                for i in range(n, len(word) + 1):
                    ngrams[n][word[i - n: i]] += freq

    stdout = codecs.getwriter('utf8')(sys.stdout)

    # for k, v in ngrams.most_common():
        # stdout.write(u'%s %d\n' % (k.ljust(5), v, ))
    for k, v in tqdm(ngrams[5].items(), total=len(ngrams[5])):
        stdout.write(u'%s %f\n' % (k.ljust(5), math.log(ngrams[4][k[:4]]) - math.log(v), ))

if __name__ == "__main__":
    main()
