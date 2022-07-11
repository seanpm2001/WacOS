@import Foundation;
@import ctypes;

#pragma clang assume_nonnull begin

#define MAIN_ACTOR __attribute__((__swift_attr__("@MainActor")))

#ifdef __SWIFT_ATTR_SUPPORTS_SENDABLE_DECLS
  #define SENDABLE __attribute__((__swift_attr__("@Sendable")))
  #define NONSENDABLE __attribute__((__swift_attr__("@_nonSendable")))
  #define ASSUME_NONSENDABLE_BEGIN _Pragma("clang attribute ASSUME_NONSENDABLE.push (__attribute__((swift_attr(\"@_nonSendable(_assumed)\"))), apply_to = any(objc_interface, record, enum))")
  #define ASSUME_NONSENDABLE_END _Pragma("clang attribute ASSUME_NONSENDABLE.pop")
#else
  // If we take this #else, we should see minor failures of some subtests,
  // but not systematic failures of everything that uses this header.
  #define SENDABLE
  #define NONSENDABLE
  #define ASSUME_NONSENDABLE_BEGIN
  #define ASSUME_NONSENDABLE_END
#endif

#define NS_ENUM(_type, _name) enum _name : _type _name; \
  enum __attribute__((enum_extensibility(open))) _name : _type
#define NS_OPTIONS(_type, _name) enum _name : _type _name; \
  enum __attribute__((enum_extensibility(open), flag_enum)) _name : _type
#define NS_ERROR_ENUM(_type, _name, _domain)  \
  enum _name : _type _name; enum __attribute__((ns_error_domain(_domain))) _name : _type
#define NS_STRING_ENUM __attribute((swift_newtype(enum)))
#define NS_EXTENSIBLE_STRING_ENUM __attribute__((swift_wrapper(struct)))

typedef NSString *Flavor NS_EXTENSIBLE_STRING_ENUM;

@protocol ServiceProvider
@property(readonly) NSArray<NSString *> *allOperations;
-(void)allOperationsWithCompletionHandler:(void (^)(NSArray<NSString *> *))completion;
@end

typedef void (^CompletionHandler)(NSString * _Nullable, NSString * _Nullable_result, NSError * _Nullable);

@interface SlowServer : NSObject <ServiceProvider>
-(void)doSomethingSlow:(NSString *)operation completionHandler:(void (^)(NSInteger))handler;
-(void)doSomethingDangerous:(NSString *)operation completionHandler:(void (^ _Nullable)(NSString *_Nullable, NSError * _Nullable))handler;
-(void)checkAvailabilityWithCompletionHandler:(void (^)(BOOL isAvailable))completionHandler;
-(void)anotherExampleWithCompletionBlock:(void (^)(NSString *))block;
-(void)finalExampleWithReplyTo:(void (^)(NSString *))block;
-(void)replyingOperation:(NSString *)operation replyTo:(void (^)(NSString *))block;
-(void)findAnswerAsynchronously:(void (^)(NSString *_Nullable, NSError * _Nullable))handler __attribute__((swift_name("findAnswer(completionHandler:)")));
-(BOOL)findAnswerFailinglyWithError:(NSError * _Nullable * _Nullable)error completion:(void (^)(NSString *_Nullable, NSError * _Nullable))handler __attribute__((swift_name("findAnswerFailingly(completionHandler:)")));
-(void)findQAndAWithCompletionHandler:(void (^)(NSString *_Nullable_result, NSString *_Nullable answer, NSError * _Nullable))handler;
-(void)findQuestionableAnswersWithCompletionHandler:(CompletionHandler)handler;
-(void)doSomethingFun:(NSString *)operation then:(void (^)())completionHandler;
-(void)getFortuneAsynchronouslyWithCompletionHandler:(void (^)(NSString *_Nullable, NSError * _Nullable))handler;
-(void)getMagicNumberAsynchronouslyWithSeed:(NSInteger)seed completionHandler:(void (^)(NSInteger, NSError * _Nullable))handler;
@property(readwrite) void (^completionHandler)(NSInteger);

-(void)findMultipleAnswersWithCompletionHandler:(void (^)(NSString *_Nullable, NSInteger, NSError * _Nullable))handler __attribute__((swift_name("findMultipleAnswers(completionHandler:)")));

-(void)findDifferentlyFlavoredBooleansWithCompletionHandler:(void (^)(BOOL wholeMilk, _Bool onePercent, NSError *_Nullable))handler __attribute__((swift_name("findDifferentlyFlavoredBooleans(completionHandler:)")));

-(void)doSomethingConflicted:(NSString *)operation completionHandler:(void (^)(NSInteger))handler;
-(NSInteger)doSomethingConflicted:(NSString *)operation;
-(void)server:(NSString *)name restartWithCompletionHandler:(void (^)(void))block;
-(void)server:(NSString *)name atPriority:(double)priority restartWithCompletionHandler:(void (^)(void))block;

-(void)poorlyNamed:(NSString *)operation completionHandler:(void (^)(NSInteger))handler __attribute__((swift_async_name("bestName(_:)")));

-(void)customizedWithString:(NSString *)operation completionHandler:(void (^)(NSInteger))handler __attribute__((swift_name("customize(with:completionHandler:)"))) __attribute__((swift_async_name("customize(_:)")));

-(void)unavailableMethod __attribute__((__swift_attr__("@_unavailableFromAsync")));
-(void)unavailableMethodWithMessage __attribute__((__swift_attr__("@_unavailableFromAsync(message: \"Blarpy!\")")));

-(void)dance:(NSString *)step andThen:(void (^)(NSString *))doSomething __attribute__((swift_async(not_swift_private,2)));
-(void)leap:(NSInteger)height andThen:(void (^)(NSString *))doSomething __attribute__((swift_async(swift_private,2)));

-(void)repeatTrick:(NSString *)trick completionHandler:(void (^)(NSInteger))handler __attribute__((swift_async(none)));

-(void)doSomethingSlowNullably:(NSString *)operation completionHandler:(void (^ _Nullable)(NSInteger))handler;
-(void)findAnswerNullably:(NSString *)operation completionHandler:(void (^ _Nullable)(NSString *))handler;
-(void)doSomethingDangerousNullably:(NSString *)operation completionHandler:(void (^ _Nullable)(NSString *_Nullable, NSError *_Nullable))handler;

// rdar://72604599
- (void)stopRecordingWithHandler:(nullable void (^)(NSObject *_Nullable_result x, NSError *_Nullable error))handler __attribute__((swift_async_name("stopRecording()"))) __attribute__((swift_async(not_swift_private, 1)));

// rdar://73798726
- (void)getSomeObjectWithCompletionHandler:(nullable void (^)(NSObject *_Nullable x, NSError *_Nullable error))handler;

- (void)performVoid2VoidWithCompletion:(void (^ _Nonnull)(void (^ _Nonnull)(void)))completion;
- (void)performId2VoidWithCompletion:(void (^ _Nonnull)(void (^ _Nonnull)(id _Nonnull)))completion;
- (void)performId2IdWithCompletion:(void (^ _Nonnull)(id _Nonnull (^ _Nonnull)(id _Nonnull)))completion;
- (void)performNSString2NSStringWithCompletion:(void (^ _Nonnull)(NSString * _Nonnull (^ _Nonnull)(NSString * _Nonnull)))completion;
- (void)performNSString2NSStringNSStringWithCompletion:(void (^ _Nonnull)(NSString * _Nonnull (^ _Nonnull)(NSString * _Nonnull), NSString * _Nonnull))completion;
- (void)performId2VoidId2VoidWithCompletion:(void (^ _Nonnull)(void (^ _Nonnull)(id _Nonnull), void (^ _Nonnull)(id _Nonnull)))completion;

-(void)oldAPIWithCompletionHandler:(void (^ _Nonnull)(NSString *_Nullable, NSError *_Nullable))handler __attribute__((availability(macosx, deprecated=10.14)));

-(void)someAsyncMethodWithBlock:(void (^ _Nonnull)(NSString *_Nullable, NSError *_Nullable))completionHandler;

// Property & async method overloading
-(void)getOperationsWithCompletionHandler:(void (^)(NSArray<NSString *> *))handler;

@property (readonly, nonatomic) NSArray<NSString *> *operations;

-(void)doSomethingFlaggyWithCompletionHandler:(void (^)(BOOL, NSString *_Nullable, NSError *_Nullable))completionHandler __attribute__((swift_async_error(nonzero_argument, 1)));
-(void)doSomethingZeroFlaggyWithCompletionHandler:(void (^)(NSString *_Nullable, BOOL, NSError *_Nullable))completionHandler __attribute__((swift_async_error(zero_argument, 2)));
-(void)doSomethingMultiResultFlaggyWithCompletionHandler:(void (^)(BOOL, NSString *_Nullable, NSError *_Nullable, NSString *_Nullable))completionHandler __attribute__((swift_async_error(zero_argument, 1)));

-(void)runOnMainThreadWithCompletionHandler:(MAIN_ACTOR void (^ _Nullable)(NSString *))completion;

// Both would be imported as the same decl - require swift_async(none) on one
-(void)asyncImportSame:(NSString *)operation completionHandler:(void (^)(NSInteger))handler;
-(void)asyncImportSame:(NSString *)operation replyTo:(void (^)(NSInteger))handler __attribute__((swift_async(none)));

-(void)overridableButRunsOnMainThreadWithCompletionHandler:(MAIN_ACTOR void (^ _Nullable)(NSString *))completion;
- (void)obtainClosureWithCompletionHandler:(void (^)(void (^_Nullable)(void),
                                                     NSError *_Nullable,
                                                     BOOL))completionHandler
    __attribute__((swift_async_error(zero_argument, 3)));
- (void)getIceCreamFlavorWithCompletionHandler:
    (void (^)(Flavor flavor, NSError *__nullable error))completionHandler;
@end

@protocol RefrigeratorDelegate<NSObject>
- (void)someoneDidOpenRefrigerator:(id)fridge;
- (void)refrigerator:(id)fridge didGetFilledWithItems:(NSArray *)items;
- (void)refrigerator:(id)fridge didGetFilledWithIntegers:(NSInteger *)items count:(NSInteger)count;
- (void)refrigerator:(id)fridge willAddItem:(id)item;
- (BOOL)refrigerator:(id)fridge didRemoveItem:(id)item;
@end

@protocol ConcurrentProtocol
-(void)askUserToSolvePuzzle:(NSString *)puzzle completionHandler:(void (^ _Nullable)(NSString * _Nullable, NSError * _Nullable))completionHandler;

@optional
-(void)askUserToJumpThroughHoop:(NSString *)hoop completionHandler:(void (^ _Nullable)(NSString *))completionHandler;
@end

@protocol ProtocolWithSwiftAttributes
-(void)independentMethod __attribute__((__swift_attr__("@actorIndependent")));
-(void)nonisolatedMethod __attribute__((__swift_attr__("nonisolated")));
-(void)mainActorMethod __attribute__((__swift_attr__("@MainActor")));
-(void)uiActorMethod __attribute__((__swift_attr__("@UIActor")));

@optional
-(void)missingAtAttributeMethod __attribute__((__swift_attr__("MainActor")));
@end

@protocol OptionalObserver <NSObject>
@optional
- (void)hello:(NSObject *)session completion:(void (^)(BOOL answer))completion;
- (BOOL)hello:(NSObject *)session;
@end

@protocol RequiredObserverOnlyCompletion <NSObject>
- (void)hello:(void (^)(BOOL answer))completion;
@end

@protocol RequiredObserver <RequiredObserverOnlyCompletion>
- (BOOL)hello;
@end

@protocol Rollable <NSObject>
- (void)rollWithCompletionHandler: (void (^)(void))completionHandler;
@end

typedef void ( ^ObjCErrorHandler )( NSError * _Nullable inError );

@protocol ObjCClub
- (void) activateWithCompletion:(ObjCErrorHandler) inCompletion;
@end

@protocol LabellyProtocol
  - (void) myMethod:(NSInteger)value1 newFoo:(NSInteger)value2 completion:(ObjCErrorHandler)completion;
  - (void) myMethod:(NSInteger)value1 foo:(NSInteger)value2;
@end

@interface GenericObject<T> : NSObject
- (void)doSomethingWithCompletionHandler:(void (^)(T _Nullable_result, NSError * _Nullable))completionHandler;
- (void)doAnotherThingWithCompletionHandler:(void (^)(GenericObject<T> *_Nullable))completionHandler;
@end

#define MAGIC_NUMBER 42


__attribute__((__swift_attr__("@MainActor")))
@interface NXView : NSObject
-(void)onDisplay;
@end

@interface NXButton: NXView
-(void)onButtonPress;
@end

// Do something concurrently, but without escaping.
void doSomethingConcurrently(__attribute__((noescape)) SENDABLE void (^block)(void));



void doSomethingConcurrentlyButUnsafe(__attribute__((noescape)) __attribute__((swift_attr("@Sendable"))) void (^block)(void));


MAIN_ACTOR MAIN_ACTOR __attribute__((__swift_attr__("@MainActor"))) @protocol TripleMainActor
@end

@protocol ProtocolWithAsync
- (void)protocolMethodWithCompletionHandler:(void (^)(void))completionHandler;
- (void)customAsyncNameProtocolMethodWithCompletionHandler:(void (^)(void))completionHandler __attribute__((swift_async_name("customAsyncName()")));
@end

@interface ClassWithAsync: NSObject <ProtocolWithAsync>
- (void)instanceMethodWithCompletionHandler:(void (^)(void))completionHandler __attribute__((swift_async_name("instanceAsync()")));
@end

SENDABLE @interface SendableClass : NSObject @end

NONSENDABLE @interface NonSendableClass : NSObject @end

ASSUME_NONSENDABLE_BEGIN

SENDABLE @interface AuditedSendable : NSObject @end
@interface AuditedNonSendable : NSObject @end
NONSENDABLE SENDABLE @interface AuditedBoth : NSObject @end

typedef NS_ENUM(unsigned, SendableEnum) {
  SendableEnumFoo, SendableEnumBar
};
typedef NS_ENUM(unsigned, NonSendableEnum) {
  NonSendableEnumFoo, NonSendableEnumBar
} NONSENDABLE;

typedef NS_OPTIONS(unsigned, SendableOptions) {
  SendableOptionsFoo = 1 << 0, SendableOptionsBar = 1 << 1
};
typedef NS_OPTIONS(unsigned, NonSendableOptions) {
  NonSendableOptionsFoo = 1 << 0, NonSendableOptionsBar = 1 << 1
} NONSENDABLE;

NSString *SendableErrorDomain, *NonSendableErrorDomain;
typedef NS_ERROR_ENUM(unsigned, SendableErrorCode, SendableErrorDomain) {
  SendableErrorCodeFoo, SendableErrorCodeBar
};
typedef NS_ERROR_ENUM(unsigned, NonSendableErrorCode, NonSendableErrorDomain) {
  NonSendableErrorCodeFoo, NonSendableErrorCodeBar
} NONSENDABLE;
// expected-warning@-3 {{cannot make error code type 'NonSendableErrorCode' non-sendable because Swift errors are always sendable}}

typedef NSString *SendableStringEnum NS_STRING_ENUM;
typedef NSString *NonSendableStringEnum NS_STRING_ENUM NONSENDABLE;

typedef NSString *SendableStringStruct NS_EXTENSIBLE_STRING_ENUM;
typedef NSString *NonSendableStringStruct NS_EXTENSIBLE_STRING_ENUM NONSENDABLE;

ASSUME_NONSENDABLE_END

#pragma clang assume_nonnull end
