// This source file is part of the Swift.org open source project
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

// RUN: not %target-swift-frontend %s -parse
class Foo<T>: NSObject {
init(foo: T) {
A {
}
struct B : A {
struct C<D, E: A where D.C => String {
{
}
{
g) {
h  }
}
protocol f {
}
func a<T>()T, T== f.h> {
}
protocol A {
func b(B)
}
struct X<Y> : A {
func b(b: X.Type) {
