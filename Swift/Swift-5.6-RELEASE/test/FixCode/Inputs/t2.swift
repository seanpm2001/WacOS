func foo2() {
	class Base {}
	class Derived : Base {}

	var b : Base
	b as Derived
	undeclared_foo2
}
