// This source file is part of the Swift.org open source project
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

// RUN: not %target-swift-frontend %s -parse
{struct A:OptionSetType
B{struct A
c{_{
enum S{class a{
enum S{
class d{
{
}enum B{struct e{
{
}enum b{class c<T where a:a{
class a{struct A:OptionSetType
