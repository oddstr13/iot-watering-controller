#!/usr/bin/env python
# -*- coding: utf-8 -*-
x = [
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',

    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_', '.', '~', ':'
]

x = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',]

import more_itertools as mit

y = sorted(set([ord(c) for c in x]))

z = [list(group) for group in mit.consecutive_groups(y)]

print()
print("If in range:")
print(' or '.join(mit.collapse(["({0} <= c and c <= {1})".format(i[0], i[-1]) if len(i) > 2 else ["c == {0}".format(l) for l in i] for i in z])))
print(' or '.join(mit.collapse(["('{0}' <= c and c <= '{1}')".format(chr(i[0]), chr(i[-1])) if len(i) > 2 else ["c == '{0}'".format(chr(l)) for l in i] for i in z])))

print()
print("If outside range:")
print(' and '.join(mit.collapse(["(c < {0} or c > {1})".format(i[0], i[-1]) if len(i) > 2 else ["c != {0}".format(l) for l in i] for i in z])))
print(' and '.join(mit.collapse(["(c < '{0}' or c > '{1}')".format(chr(i[0]), chr(i[-1])) if len(i) > 2 else ["c != '{0}'".format(chr(l)) for l in i] for i in z])))
