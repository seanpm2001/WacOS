from test_support import TestFailed

class base_set:

	def __init__(self, el):
		self.el = el

class set(base_set):

	def __contains__(self, el):
		return self.el == el

class seq(base_set):

	def __getitem__(self, n):
		return [self.el][n]

def check(ok, *args):
    if not ok:
        raise TestFailed, " ".join(map(str, args))

a = base_set(1)
b = set(1)
c = seq(1)

check(1 in b, "1 not in set(1)")
check(0 not in b, "0 in set(1)")
check(1 in c, "1 not in seq(1)")
check(0 not in c, "0 in seq(1)")

try:
	1 in a
	check(0, "in base_set did not raise error")
except AttributeError:
	pass

try:
	1 not in a
	check(0, "not in base_set did not raise error")
except AttributeError:
	pass

# Test char in string

check('c' in 'abc', "'c' not in 'abc'")
check('d' not in 'abc', "'d' in 'abc'")

try:
	'' in 'abc'
	check(0, "'' in 'abc' did not raise error")
except TypeError:
	pass

try:
	'ab' in 'abc'
	check(0, "'ab' in 'abc' did not raise error")
except TypeError:
	pass

try:
	None in 'abc'
	check(0, "None in 'abc' did not raise error")
except TypeError:
	pass

# Test char in Unicode

check('c' in u'abc', "'c' not in u'abc'")
check('d' not in u'abc', "'d' in u'abc'")

try:
	'' in u'abc'
	check(0, "'' in u'abc' did not raise error")
except TypeError:
	pass

try:
	'ab' in u'abc'
	check(0, "'ab' in u'abc' did not raise error")
except TypeError:
	pass

try:
	None in u'abc'
	check(0, "None in u'abc' did not raise error")
except TypeError:
	pass

# Test Unicode char in Unicode

check(u'c' in u'abc', "u'c' not in u'abc'")
check(u'd' not in u'abc', "u'd' in u'abc'")

try:
	u'' in u'abc'
	check(0, "u'' in u'abc' did not raise error")
except TypeError:
	pass

try:
	u'ab' in u'abc'
	check(0, "u'ab' in u'abc' did not raise error")
except TypeError:
	pass

# Test Unicode char in string

check(u'c' in 'abc', "u'c' not in 'abc'")
check(u'd' not in 'abc', "u'd' in 'abc'")

try:
	u'' in 'abc'
	check(0, "u'' in 'abc' did not raise error")
except TypeError:
	pass

try:
	u'ab' in 'abc'
	check(0, "u'ab' in 'abc' did not raise error")
except TypeError:
	pass

# A collection of tests on builtin sequence types
a = range(10)
for i in a:
	check(i in a, "%s not in %s" % (`i`, `a`))
check(16 not in a, "16 not in %s" % `a`)
check(a not in a, "%s not in %s" % (`a`, `a`))

a = tuple(a)
for i in a:
	check(i in a, "%s not in %s" % (`i`, `a`))
check(16 not in a, "16 not in %s" % `a`)
check(a not in a, "%s not in %s" % (`a`, `a`))

class Deviant1:
	"""Behaves strangely when compared

	This class is designed to make sure that the contains code
	works when the list is modified during the check.
	"""

	aList = range(15)
	
	def __cmp__(self, other):
		if other == 12:
			self.aList.remove(12)
			self.aList.remove(13)
			self.aList.remove(14)
		return 1

check(Deviant1() not in Deviant1.aList, "Deviant1 failed")

class Deviant2:
	"""Behaves strangely when compared

	This class raises an exception during comparison.  That in
	turn causes the comparison to fail with a TypeError.
	"""

	def __cmp__(self, other):
		if other == 4:
			raise RuntimeError, "gotcha"

try:
	check(Deviant2() not in a, "oops")
except TypeError:
	pass
