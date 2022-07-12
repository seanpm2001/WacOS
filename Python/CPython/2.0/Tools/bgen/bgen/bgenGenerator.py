from bgenOutput import *
from bgenType import *
from bgenVariable import *


Error = "bgenGenerator.Error"


# Strings to specify argument transfer modes in generator calls
IN = "in"
OUT = "out"
INOUT = IN_OUT = "in-out"


class BaseFunctionGenerator:

	def __init__(self, name, condition=None):
		print "<--", name
		self.name = name
		self.prefix = name
		self.objecttype = "PyObject" # Type of _self argument to function
		self.condition = condition

	def setprefix(self, prefix):
		self.prefix = prefix

	def generate(self):
		print "-->", self.name
		if self.condition:
			Output()
			Output(self.condition)
		self.functionheader()
		self.functionbody()
		self.functiontrailer()
		if self.condition:
			Output("#endif")

	def functionheader(self):
		Output()
		Output("static PyObject *%s_%s(_self, _args)",
		       self.prefix, self.name)
		IndentLevel()
		Output("%s *_self;", self.objecttype)
		Output("PyObject *_args;")
		DedentLevel()
		OutLbrace()
		Output("PyObject *_res = NULL;")

	def functionbody(self):
		Output("/* XXX To be provided */")

	def functiontrailer(self):
		OutRbrace()

	def reference(self, name = None):
		if name is None:
			name = self.name
		docstring = self.docstring()
		if self.condition:
			Output()
			Output(self.condition)		
		Output("{\"%s\", (PyCFunction)%s_%s, 1,", name, self.prefix, self.name)
		Output(" %s},", stringify(docstring))
		if self.condition:
			Output("#endif")

	def docstring(self):
		return None


_stringify_map = {'\n': '\\n', '\t': '\\t', '\r': '\\r', '\b': '\\b',
                  '\e': '\\e', '\a': '\\a', '\f': '\\f', '"': '\\"'}
def stringify(str):
	if str is None: return "NULL"
	res = '"'
	map = _stringify_map
	for c in str:
		if map.has_key(c): res = res + map[c]
		elif ' ' <= c <= '~': res = res + c
		else: res = res + '\\%03o' % ord(c)
	res = res + '"'
	return res


class ManualGenerator(BaseFunctionGenerator):

	def __init__(self, name, body, condition=None):
		BaseFunctionGenerator.__init__(self, name, condition=condition)
		self.body = body

	def functionbody(self):
		Output("%s", self.body)
		
	def setselftype(self, selftype, itselftype):
		self.objecttype = selftype
		self.itselftype = itselftype


class FunctionGenerator(BaseFunctionGenerator):

	def __init__(self, returntype, name, *argumentList, **conditionlist):
		BaseFunctionGenerator.__init__(self, name, **conditionlist)
		self.returntype = returntype
		self.argumentList = []
		self.setreturnvar()
		self.parseArgumentList(argumentList)
		self.prefix     = "XXX"    # Will be changed by setprefix() call
		self.itselftype = None     # Type of _self->ob_itself, if defined

	def setreturnvar(self):
		if self.returntype:
			self.rv = self.makereturnvar()
			self.argumentList.append(self.rv)
		else:
			self.rv = None
	
	def makereturnvar(self):
		return Variable(self.returntype, "_rv", OutMode)

	def setselftype(self, selftype, itselftype):
		self.objecttype = selftype
		self.itselftype = itselftype

	def parseArgumentList(self, argumentList):
		iarg = 0
		for type, name, mode in argumentList:
			iarg = iarg + 1
			if name is None: name = "_arg%d" % iarg
			arg = Variable(type, name, mode)
			self.argumentList.append(arg)
	
	def docstring(self):
		import string
		input = []
		output = []
		for arg in self.argumentList:
			if arg.flags == ErrorMode or arg.flags == SelfMode:
				continue
			if arg.type == None:
				str = 'void'
			else:
				if hasattr(arg.type, 'typeName'):
					typeName = arg.type.typeName
					if typeName is None: # Suppressed type
						continue
				else:
					typeName = "?"
					print "Nameless type", arg.type
					
				str = typeName + ' ' + arg.name
			if arg.mode in (InMode, InOutMode):
				input.append(str)
			if arg.mode in (InOutMode, OutMode):
				output.append(str)
		if not input:
			instr = "()"
		else:
			instr = "(%s)" % string.joinfields(input, ", ")
		if not output or output == ["void"]:
			outstr = "None"
		else:
			outstr = "(%s)" % string.joinfields(output, ", ")
		return instr + " -> " + outstr
	
	def functionbody(self):
		self.declarations()
		self.getargs()
		self.callit()
		self.checkit()
		self.returnvalue()

	def declarations(self):
		for arg in self.argumentList:
			arg.declare()

	def getargs(self):
		fmt = ""
		lst = ""
		sep = ",\n" + ' '*len("if (!PyArg_ParseTuple(")
		for arg in self.argumentList:
			if arg.flags == SelfMode:
				continue
			if arg.mode in (InMode, InOutMode):
				fmt = fmt + arg.getargsFormat()
				args = arg.getargsArgs()
				if args:
					lst = lst + sep + args
		Output("if (!PyArg_ParseTuple(_args, \"%s\"%s))", fmt, lst)
		IndentLevel()
		Output("return NULL;")
		DedentLevel()
		for arg in self.argumentList:
			if arg.flags == SelfMode:
				continue
			if arg.mode in (InMode, InOutMode):
				arg.getargsCheck()

	def callit(self):
		args = ""
		if self.rv:
			s = "%s = %s(" % (self.rv.name, self.name)
		else:
			s = "%s(" % self.name
		sep = ",\n" + ' '*len(s)
		for arg in self.argumentList:
			if arg is self.rv:
				continue
			s = arg.passArgument()
			if args: s = sep + s
			args = args + s
		if self.rv:
			Output("%s = %s(%s);",
			       self.rv.name, self.name, args)
		else:
			Output("%s(%s);", self.name, args)

	def checkit(self):
		for arg in self.argumentList:
			arg.errorCheck()

	def returnvalue(self):
		fmt = ""
		lst = ""
		sep = ",\n" + ' '*len("return Py_BuildValue(")
		for arg in self.argumentList:
			if not arg: continue
			if arg.flags == ErrorMode: continue
			if arg.mode in (OutMode, InOutMode):
				fmt = fmt + arg.mkvalueFormat()
				lst = lst + sep + arg.mkvalueArgs()
		if fmt == "":
			Output("Py_INCREF(Py_None);")
			Output("_res = Py_None;");
		else:
			Output("_res = Py_BuildValue(\"%s\"%s);", fmt, lst)
		tmp = self.argumentList[:]
		tmp.reverse()
		for arg in tmp:
			if not arg: continue
			arg.cleanup()
		Output("return _res;")


class MethodGenerator(FunctionGenerator):

	def parseArgumentList(self, args):
		a0, args = args[0], args[1:]
		t0, n0, m0 = a0
		if m0 != InMode:
			raise ValueError, "method's 'self' must be 'InMode'"
		self.itself = Variable(t0, "_self->ob_itself", SelfMode)
		self.argumentList.append(self.itself)
		FunctionGenerator.parseArgumentList(self, args)


def _test():
	void = None
	eggs = Generator(void, "eggs",
	                 Variable(stringptr, 'cmd'),
	                 Variable(int, 'x'),
	                 Variable(double, 'y', InOutMode),
	                 Variable(int, 'status', ErrorMode),
	                )
	eggs.setprefix("spam")
	print "/* START */"
	eggs.generate()


if __name__ == "__main__":
	_test()
