tbls = 'static struct TQuickLZMethods* methods_qlz151[3][3] = {\n'

for level in range(1, 4):
    tbls += '{'

    for buf in (0, 100000, 1000000):
        print '#define COMPRESSION_LEVEL ', level
        print '#define STREAMING_MODE ', buf
        print '#include "y.c"'

        table = 'yqlz_1_51_' + str(level) + '_' + str(buf) + '_table'
        tbls += '&' + table + ', '

    tbls += '},\n'

tbls += '};\n'

print tbls
