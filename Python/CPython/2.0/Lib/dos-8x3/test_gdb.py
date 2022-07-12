#! /usr/bin/env python
"""Test script for the gdbm module
   Roger E. Masse
"""
    
import gdbm
from gdbm import error
from test_support import verbose, TestFailed

filename= '/tmp/delete_me'

g = gdbm.open(filename, 'c')
g['a'] = 'b'
g['12345678910'] = '019237410982340912840198242'
a = g.keys()
if verbose:
    print 'Test gdbm file keys: ', a
    
g.has_key('a')
g.close()
try:
    g['a']
except error:
    pass
else:
    raise TestFailed, "expected gdbm.error accessing closed database"
g = gdbm.open(filename, 'r')
g.close()
g = gdbm.open(filename, 'rw')
g.close()
g = gdbm.open(filename, 'w')
g.close()
g = gdbm.open(filename, 'n')
g.close()

try:
    import os
    os.unlink(filename)
except:
    pass
