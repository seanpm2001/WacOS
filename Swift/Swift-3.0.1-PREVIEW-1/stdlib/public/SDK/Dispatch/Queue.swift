//===----------------------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

// dispatch/queue.h

import SwiftShims

public final class DispatchSpecificKey<T> {
	public init() {}
}

internal class _DispatchSpecificValue<T> {
	internal let value: T
	internal init(value: T) { self.value = value }
}

public extension DispatchQueue {
	public struct Attributes : OptionSet {
		public let rawValue: UInt64
		public init(rawValue: UInt64) { self.rawValue = rawValue }

		public static let concurrent = Attributes(rawValue: 1<<1)

		@available(OSX 10.12, iOS 10.0, tvOS 10.0, watchOS 3.0, *)
		public static let initiallyInactive = Attributes(rawValue: 1<<2)

		fileprivate func _attr() -> __OS_dispatch_queue_attr? {
			var attr: __OS_dispatch_queue_attr? = nil

			if self.contains(.concurrent) {
				attr = _swift_dispatch_queue_concurrent()
			}
			if #available(OSX 10.12, iOS 10.0, tvOS 10.0, watchOS 3.0, *) {
				if self.contains(.initiallyInactive) {
					attr = __dispatch_queue_attr_make_initially_inactive(attr)
				}
			}
			return attr
		}
	}

	public enum GlobalQueuePriority {
		@available(OSX, deprecated: 10.10, message: "Use qos attributes instead")
		@available(iOS, deprecated: 8.0, message: "Use qos attributes instead")
		@available(tvOS, deprecated, message: "Use qos attributes instead")
		@available(watchOS, deprecated, message: "Use qos attributes instead")
		case high

		@available(OSX, deprecated: 10.10, message: "Use qos attributes instead")
		@available(iOS, deprecated: 8.0, message: "Use qos attributes instead")
		@available(tvOS, deprecated, message: "Use qos attributes instead")
		@available(watchOS, deprecated, message: "Use qos attributes instead")
		case `default`

		@available(OSX, deprecated: 10.10, message: "Use qos attributes instead")
		@available(iOS, deprecated: 8.0, message: "Use qos attributes instead")
		@available(tvOS, deprecated, message: "Use qos attributes instead")
		@available(watchOS, deprecated, message: "Use qos attributes instead")
		case low

		@available(OSX, deprecated: 10.10, message: "Use qos attributes instead")
		@available(iOS, deprecated: 8.0, message: "Use qos attributes instead")
		@available(tvOS, deprecated, message: "Use qos attributes instead")
		@available(watchOS, deprecated, message: "Use qos attributes instead")
		case background

		internal var _translatedValue: Int {
			switch self {
			case .high: return 2 // DISPATCH_QUEUE_PRIORITY_HIGH
			case .default: return 0 // DISPATCH_QUEUE_PRIORITY_DEFAULT
			case .low: return -2 // DISPATCH_QUEUE_PRIORITY_LOW
			case .background: return Int(Int16.min) // DISPATCH_QUEUE_PRIORITY_BACKGROUND
			}
		}
	}

	public enum AutoreleaseFrequency {
		case inherit

		@available(OSX 10.12, iOS 10.0, tvOS 10.0, watchOS 3.0, *)
		case workItem

		@available(OSX 10.12, iOS 10.0, tvOS 10.0, watchOS 3.0, *)
		case never

		internal func _attr(attr: __OS_dispatch_queue_attr?) -> __OS_dispatch_queue_attr? {
			if #available(OSX 10.12, iOS 10.0, tvOS 10.0, watchOS 3.0, *) {
				switch self {
				case .inherit: 
					// DISPATCH_AUTORELEASE_FREQUENCY_INHERIT
					return __dispatch_queue_attr_make_with_autorelease_frequency(attr, __dispatch_autorelease_frequency_t(0))
				case .workItem:
					// DISPATCH_AUTORELEASE_FREQUENCY_WORK_ITEM
					return __dispatch_queue_attr_make_with_autorelease_frequency(attr, __dispatch_autorelease_frequency_t(1))
				case .never:
					// DISPATCH_AUTORELEASE_FREQUENCY_NEVER
					return __dispatch_queue_attr_make_with_autorelease_frequency(attr, __dispatch_autorelease_frequency_t(2))
				}
			} else {
				return attr
			}
		}
	}

	public class func concurrentPerform(iterations: Int, execute work: (Int) -> Void) {
		_swift_dispatch_apply_current(UInt32(iterations), work)
	}

	public class var main: DispatchQueue {
		return _swift_dispatch_get_main_queue()
	}

	@available(OSX, deprecated: 10.10)
	@available(iOS, deprecated: 8.0)
	@available(tvOS, deprecated)
	@available(watchOS, deprecated)
	public class func global(priority: GlobalQueuePriority) -> DispatchQueue {
		return __dispatch_get_global_queue(priority._translatedValue, 0)
	}

	@available(OSX 10.10, iOS 8.0, *)
	public class func global(qos: DispatchQoS.QoSClass = .default) -> DispatchQueue {
		return __dispatch_get_global_queue(Int(qos.rawValue.rawValue), 0)
	}

	public class func getSpecific<T>(key: DispatchSpecificKey<T>) -> T? {
		let k = Unmanaged.passUnretained(key).toOpaque()
		if let p = __dispatch_get_specific(k) {
			let v = Unmanaged<_DispatchSpecificValue<T>>
				.fromOpaque(p)
				.takeUnretainedValue()
			return v.value
		}
		return nil
	}
	
	public convenience init(
		label: String,
		qos: DispatchQoS = .unspecified,
		attributes: Attributes = [],
		autoreleaseFrequency: AutoreleaseFrequency = .inherit,
		target: DispatchQueue? = nil)
	{
		var attr = attributes._attr()
		if autoreleaseFrequency != .inherit { 
			attr = autoreleaseFrequency._attr(attr: attr) 
		}
		if #available(OSX 10.10, iOS 8.0, *), qos != .unspecified {
			attr = __dispatch_queue_attr_make_with_qos_class(attr, qos.qosClass.rawValue, Int32(qos.relativePriority))
		}

		if #available(OSX 10.12, iOS 10.0, tvOS 10.0, watchOS 3.0, *) {
			self.init(__label: label, attr: attr, queue: target)
		} else {
			self.init(__label: label, attr: attr)
			if let tq = target { self.setTarget(queue: tq) }
		}
	}

	public var label: String {
		return String(validatingUTF8: __dispatch_queue_get_label(self))!
	}

	@available(OSX 10.10, iOS 8.0, *)
	public func sync(execute workItem: DispatchWorkItem) {
		// _swift_dispatch_sync preserves the @convention(block) for
		// work item blocks.
		_swift_dispatch_sync(self, workItem._block)
	}

	@available(OSX 10.10, iOS 8.0, *)
	public func async(execute workItem: DispatchWorkItem) {
		// _swift_dispatch_async preserves the @convention(block) 
		// for work item blocks.
		_swift_dispatch_async(self, workItem._block)
	}

	@available(OSX 10.10, iOS 8.0, *)
	public func async(group: DispatchGroup, execute workItem: DispatchWorkItem) {
		// _swift_dispatch_group_async preserves the @convention(block) 
		// for work item blocks.
		_swift_dispatch_group_async(group, self, workItem._block)
	}

	public func async(
		group: DispatchGroup? = nil, 
		qos: DispatchQoS = .unspecified, 
		flags: DispatchWorkItemFlags = [], 
		execute work: @escaping @convention(block) () -> Void) 
	{
		if group == nil && qos == .unspecified {
			// Fast-path route for the most common API usage
			if flags.isEmpty {
				_swift_dispatch_async(self, work)
				return
			} else if flags == .barrier {
				_swift_dispatch_barrier_async(self, work)
				return
			}
		}

		var block: @convention(block) () -> Void = work
		if #available(OSX 10.10, iOS 8.0, *), (qos != .unspecified || !flags.isEmpty) {
			let workItem = DispatchWorkItem(qos: qos, flags: flags, block: work)
			block = workItem._block
		}

		if let g = group {
			_swift_dispatch_group_async(g, self, block)
		} else {
			_swift_dispatch_async(self, block)
		}
	}

	private func _syncBarrier(block: () -> ()) {
		__dispatch_barrier_sync(self, block)
	}

	private func _syncHelper<T>(
		fn: (() -> ()) -> (), 
		execute work: () throws -> T, 
		rescue: ((Error) throws -> (T))) rethrows -> T 
	{
		var result: T?
		var error: Error?
		fn {
			do {
				result = try work()
			} catch let e {
				error = e
			}
		}
		if let e = error {
			return try rescue(e)
		} else {
			return result!
		}
	}

	@available(OSX 10.10, iOS 8.0, *)
	private func _syncHelper<T>(
		fn: (DispatchWorkItem) -> (), 
		flags: DispatchWorkItemFlags,
		execute work: () throws -> T,
		rescue: ((Error) throws -> (T))) rethrows -> T 
	{
		var result: T?
		var error: Error?
		let workItem = DispatchWorkItem(flags: flags, noescapeBlock: {
			do {
				result = try work()
			} catch let e {
				error = e 
			}
		})
		fn(workItem)
		if let e = error {
			return try rescue(e)
		} else {
			return result!
		}
	}

	public func sync<T>(execute work: () throws -> T) rethrows -> T {
		return try self._syncHelper(fn: sync, execute: work, rescue: { throw $0 })
	}

	public func sync<T>(flags: DispatchWorkItemFlags, execute work: () throws -> T) rethrows -> T {
		if flags == .barrier {
			return try self._syncHelper(fn: _syncBarrier, execute: work, rescue: { throw $0 })
		} else if #available(OSX 10.10, iOS 8.0, *), !flags.isEmpty {
			return try self._syncHelper(fn: sync, flags: flags, execute: work, rescue: { throw $0 })
		} else {
			return try self._syncHelper(fn: sync, execute: work, rescue: { throw $0 })
		}
	}

	public func asyncAfter(
		deadline: DispatchTime, 
		qos: DispatchQoS = .unspecified, 
		flags: DispatchWorkItemFlags = [], 
		execute work: @escaping @convention(block) () -> Void) 
	{
		if #available(OSX 10.10, iOS 8.0, *), qos != .unspecified || !flags.isEmpty {
			let item = DispatchWorkItem(qos: qos, flags: flags, block: work)
			_swift_dispatch_after(deadline.rawValue, self, item._block)
		} else {
			_swift_dispatch_after(deadline.rawValue, self, work)
		}
	}

	public func asyncAfter(
		wallDeadline: DispatchWallTime,
		qos: DispatchQoS = .unspecified, 
		flags: DispatchWorkItemFlags = [], 
		execute work: @escaping @convention(block) () -> Void) 
	{
		if #available(OSX 10.10, iOS 8.0, *), qos != .unspecified || !flags.isEmpty {
			let item = DispatchWorkItem(qos: qos, flags: flags, block: work)
			_swift_dispatch_after(wallDeadline.rawValue, self, item._block)
		} else {
			_swift_dispatch_after(wallDeadline.rawValue, self, work)
		}
	}

	@available(OSX 10.10, iOS 8.0, *)
	public func asyncAfter(deadline: DispatchTime, execute: DispatchWorkItem) {
		_swift_dispatch_after(deadline.rawValue, self, execute._block)
	}

	@available(OSX 10.10, iOS 8.0, *)
	public func asyncAfter(wallDeadline: DispatchWallTime, execute: DispatchWorkItem) {
		_swift_dispatch_after(wallDeadline.rawValue, self, execute._block)
	}

	@available(OSX 10.10, iOS 8.0, *)
	public var qos: DispatchQoS {
		var relPri: Int32 = 0
		let cls = DispatchQoS.QoSClass(rawValue: __dispatch_queue_get_qos_class(self, &relPri))!
		return DispatchQoS(qosClass: cls, relativePriority: Int(relPri))
	}

	public func getSpecific<T>(key: DispatchSpecificKey<T>) -> T? {
		let k = Unmanaged.passUnretained(key).toOpaque()
		if let p = __dispatch_queue_get_specific(self, k) {
			let v = Unmanaged<_DispatchSpecificValue<T>>
				.fromOpaque(p)
				.takeUnretainedValue()
			return v.value
		}
		return nil
	}

	public func setSpecific<T>(key: DispatchSpecificKey<T>, value: T) {
		let v = _DispatchSpecificValue(value: value)
		let k = Unmanaged.passUnretained(key).toOpaque()
		let p = Unmanaged.passRetained(v).toOpaque()
		__dispatch_queue_set_specific(self, k, p, _destructDispatchSpecificValue)
	}
}

private func _destructDispatchSpecificValue(ptr: UnsafeMutableRawPointer?) {
	if let p = ptr {
		Unmanaged<AnyObject>.fromOpaque(p).release()
	}
}

@_silgen_name("_swift_dispatch_queue_concurrent")
internal func _swift_dispatch_queue_concurrent() -> __OS_dispatch_queue_attr

@_silgen_name("_swift_dispatch_get_main_queue")
internal func _swift_dispatch_get_main_queue() -> DispatchQueue
