// RUN: %target-build-swift -parse %s -Xfrontend -verify
// RUN: %target-build-swift -emit-ir -g %s -DNO_ERROR > /dev/null
// REQUIRES: executable_test

// REQUIRES: objc_interop
// REQUIRES: OS=macosx

import OpenGL.GL3
_ = glGetString
_ = OpenGL.glGetString
_ = GL_COLOR_BUFFER_BIT
_ = OpenGL.GL_COLOR_BUFFER_BIT

import AppKit.NSPanGestureRecognizer

@available(OSX, introduced: 10.10)
typealias PanRecognizer = NSPanGestureRecognizer
typealias PanRecognizer2 = AppKit.NSPanGestureRecognizer

#if !NO_ERROR
_ = glVertexPointer // expected-error{{use of unresolved identifier 'glVertexPointer'}}
#endif
